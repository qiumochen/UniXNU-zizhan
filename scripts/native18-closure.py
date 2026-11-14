#!/usr/bin/env python3
"""Derive a fresh stock-kext import map and report native closure gaps.

Uses pinned instruction/PAC matching with optional independently built RELEASE
naming references. Missing imports remain missing; this tool emits no kernel
collection, boot image, stubs or device operations.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
PROFILE = tomllib.loads((ROOT / "configs/zizhan-ios18e-inputs.toml").read_text())


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def select_reference_identities(references, names):
    """Exact-code reference identities take precedence; graph guesses do not seed names.

    A graph match from one reference previously won by argument order over
    another reference's actual complete-code identity. Conflicting exact
    identities remain refused, including ambiguous aliases in the final
    source image. Weak rows stay in the audit only.
    """
    exact = {}
    rejected = []
    prefix = "Unique bidirectional exact relocated-instruction body after entry/return PAC framing only;"
    for label, reference in references:
        if reference.get("build") != "22G100" or reference.get("reference_role") != "names-only":
            raise RuntimeError("Wrong native naming-reference role/build")
        for row in reference["functions"]:
            complete_operands = row.get("operand_proof") == "complete-v1" and row.get("evidence", "").startswith(
                "Unique bidirectional complete instruction body with normalized compiler PAC checks;")
            if row.get("review_only") or row.get("binding_authorized") is False or \
                    not (row.get("evidence", "").startswith(prefix) or complete_operands):
                rejected.append({"original": row["original"], "reference": label, "names": row["names"],
                                 "reason": "graph/string/order identity is a candidate, not independently exact code"})
                continue
            targets = {names[name] for name in row["names"] if name in names}
            exact.setdefault(row["original"], []).append((label, row, targets))
    qualified = []
    for address, rows in exact.items():
        targets = set().union(*(values for _, _, values in rows))
        if len(targets) != 1 or any(len(values) != 1 for _, _, values in rows):
            rejected.append({"original": address, "reason": "exact references disagree or source names do not resolve uniquely",
                             "references": [{"reference": label, "names": row["names"]} for label, row, _ in rows]})
            continue
        first = min((row for _, row, _ in rows), key=lambda row: (row["names"], row["instructions"]))
        item = dict(first)
        item["supporting_exact_references"] = [label for label, _, _ in rows]
        qualified.append(item)
    return qualified, rejected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("native-source", "input-root", "native-build", "parent-root", "prelink-kit", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--reference-build", type=Path)
    parser.add_argument("--reference-map", type=Path)
    parser.add_argument("--name-reference", type=Path, action="append", default=[])
    parser.add_argument("--anchor", type=Path, action="append", default=[])
    parser.add_argument("--class-abi", type=Path,
                        help="Optional complete core-class PAC tables from this exact source build")
    args = parser.parse_args()
    source, work, out = args.native_source.resolve(), args.native_build.resolve(), args.out.resolve()
    kit = args.prelink_kit.resolve()
    for path, expected in ((source, PROFILE["source"]["commit"]), (kit, PROFILE["prelink"]["commit"])):
        if subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip() != expected:
            parser.error("Wrong pinned source revision: " + str(path))
    receipt = json.loads((work / "host-core.json").read_text())
    if receipt.get("returncode") != 0 or receipt.get("configuration", "DEVELOPMENT") != "DEVELOPMENT":
        parser.error("A completed DEVELOPMENT source link is required")
    native = work / "artifacts/mach.development.pong.unstripped.noctf"
    hashes = {Path(row["path"]).name: row["sha256"] for row in receipt["artifacts"]}
    if sha(native) != hashes.get(native.name):
        parser.error("Native kernel differs from its actual build receipt")
    stock = json.loads((args.input_root / "native-inputs.json").read_text())
    original = (args.input_root / stock["kernelcache"]["path"]).resolve()
    if stock["profile"] != PROFILE["ipsw"] or sha(original) != stock["kernelcache"]["sha256"]:
        parser.error("Native stock kernel/restore selection differs from its receipt")
    if bool(args.reference_build) != bool(args.reference_map):
        parser.error("Both --reference-build and its fresh --reference-map are required")
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error("Use a new machine-local output directory")
    out.mkdir(parents=True)
    (out / "kit-root/kits").mkdir(parents=True)
    (out / "kit-root/kits/XnuPrelink").symlink_to(kit, target_is_directory=True)
    parents = [args.parent_root / "native-parent.json",
               args.parent_root / "parent-references/IOAESAccelerator.json",
               args.parent_root / "parent-references/IOAcceleratorES.json"]
    if args.class_abi:
        metadata = json.loads((args.class_abi / "receipt.json").read_text())
        if (metadata.get("stock_sha256"), metadata.get("native_sha256")) != (sha(original), sha(native)):
            parser.error("Core-class evidence belongs to different actual kernel inputs")
        for row in metadata["results"]:
            if row.get("qualified"):
                parent = args.class_abi / row["path"]
                evidence = json.loads(parent.read_text())
                if not evidence.get("abi_compatible") or evidence.get("classname") != row["classname"]:
                    parser.error("Refused or inconsistent core-class table: " + str(parent))
                parents.append(parent)
    sys.path.insert(0, str(kit))
    from kc_image import Image
    symbol_values = {}
    for address, rows in Image(native.read_bytes()).symbols().items():
        for name in rows:
            symbol_values.setdefault(name, set()).add(address)
    actual_names = {name: next(iter(values)) for name, values in symbol_values.items() if len(values) == 1}
    references = [(str(path.resolve()), json.loads(path.read_text())) for path in args.name_reference]
    for _, reference in references:
        if reference.get("operand_proof") == "complete-v1" and (
                reference.get("stock_sha256"), reference.get("dependency_native_sha256")) != (sha(original), sha(native)):
            parser.error("Complete reference operand proof belongs to different actual inputs")
    selected_names, rejected_names = select_reference_identities(references, actual_names)
    reference_names = out / "exact-reference-names.json"
    reference_names.write_text(json.dumps({"build": "22G100", "reference_role": "names-only", "functions": selected_names}, indent=2) + "\n")
    (out / "reference-name-audit.json").write_text(json.dumps({"policy": "exact-code identities only; conflicting exact references refuse; no input-order precedence",
                                                              "qualified": len(selected_names), "rejected": rejected_names}, indent=2) + "\n")
    mapper = source / "scripts/map-pong-fileset18e.py"
    generated_mapper = out / "map-with-qualified-reference-inputs.py"
    text = mapper.read_text()
    insert_path = "import types\n"
    order = "        print('Release order anchors',matcher.anchor_reference_order(reference,pairing),flush=True)"
    if text.count(insert_path) != 1 or text.count(order) != 1:
        raise RuntimeError("Review changed native reference naming/order boundaries")
    text = text.replace(insert_path, insert_path + "sys.path.insert(0, " + repr(str(source / "scripts")) + ")\n", 1)
    text = text.replace(order, "        print('Release order anchors disabled: order/graph evidence cannot establish KPI identity',flush=True)")
    generated_mapper.write_text(text)
    mapper = generated_mapper
    if any(any(row.get("operational_equivalence_only") or row.get("final_import_only") for row in json.loads(path.read_text()).get("functions", [])) for path in args.anchor):
        # Equal-effect functions are valid final import implementations, but
        # their source spelling is not semantic identity for graph inference.
        # Bind only after every heuristic/data propagation pass has finished.
        text = generated_mapper.read_text()
        early = "        for row in anchor['functions']:\n"
        final = "    result = matcher.report(data)\n"
        if text.count(early) != 1 or text.count(final) != 1:
            raise RuntimeError("Review changed native matcher anchor/propagation boundaries")
        text = text.replace(early, early + "            if row.get('operational_equivalence_only') or row.get('final_import_only'):continue\n")
        late = ("    for path in a.anchor:\n"
                "        for row in json.loads(path.read_text()).get('functions',[]):\n"
                "            if not (row.get('operational_equivalence_only') or row.get('final_import_only')):continue\n"
                "            if not row.get('evidence') or row['name'] not in names:\n"
                "                raise RuntimeError('Late equivalent import requires real source/evidence')\n"
                "            original,target=int(row['original'],16),names[row['name']]\n"
                "            existing=matcher.pairs.get(original)\n"
                "            if existing is not None and existing!=target:\n"
                "                matcher.conflicts.append({'kind':'final-contract-corrects-provisional','orig':hex(original),'previous_native':hex(existing),'native':hex(target)})\n"
                "                del matcher.pairs[original]\n"
                "                matcher.how.pop(original,None)\n"
                "                matcher.reverse={}\n"
                "                for original_key,native_value in matcher.pairs.items():matcher.reverse.setdefault(native_value,original_key)\n"
                "            if original not in matcher.pairs:matcher.bind(original,target,'reviewed')\n")
        text = text.replace(final, late + final)
        generated_mapper.write_text(text)
        mapper = generated_mapper
    command = [sys.executable, str(mapper),
               "--unixnu", str(out / "kit-root"), "--original", str(original),
               "--native", str(native), "--out", str(out / "pairing.json")]
    for parent in parents:
        command += ["--parent", str(parent.resolve())]
    if args.reference_build:
        reference = args.reference_build.resolve()
        result = json.loads((reference / "host-core.json").read_text())
        if result.get("returncode") != 0 or result.get("configuration") != "RELEASE":
            parser.error("Naming reference must be an actual completed RELEASE build")
        image = reference / "artifacts/mach.release.pong.unstripped.noctf"
        pairing = json.loads(args.reference_map.read_text())
        if Path(pairing["original"]).resolve() != original or Path(pairing["native"]).resolve() != image:
            parser.error("RELEASE reference pairing paths differ from actual inputs")
        pairing["functions"] = {key: row for key, row in pairing["functions"].items()
                                if row["how"] in ("shape", "pac-checked-shape", "reference-shape")}
        # Data identities from a graph-populated reference inherit its
        # uncertainty. Actual candidate class/descriptor/initializer data
        # evidence is supplied independently via qualified anchors instead.
        pairing["data"] = {}
        filtered = out / "exact-release-reference-map.json"
        filtered.write_text(json.dumps(pairing, indent=2) + "\n")
        command += ["--reference-kernel", str(image), "--reference-map", str(filtered)]
    if args.name_reference:
        command += ["--name-reference", str(reference_names)]
    for anchor in args.anchor:
        evidence = json.loads(anchor.read_text())
        if (evidence.get("stock_sha256"), evidence.get("native_sha256")) != (sha(original), sha(native)):
            parser.error("Anchor evidence belongs to different actual kernel inputs: " + str(anchor))
        command += ["--anchor", str(anchor.resolve())]
    report = {"started_at": datetime.now().astimezone().isoformat(), "source": PROFILE["source"],
              "stock_sha256": sha(original), "native_sha256": sha(native),
              "scope": "host import analysis only; no bootable fileset emitted", "bootable": False,
              "device_access": False, "commands": [command]}
    if generated_mapper:
        report["mapper_adaptation"] = {"sha256": sha(generated_mapper),
                                       "scope": "qualified exact-reference identities with cross-reference ambiguity refusal; no order/data-graph transfer; operational duplicates bind only after graph/data propagation"}
    report["reference_identity_policy"] = {"qualified_names": len(selected_names), "audit_sha256": sha(out / "reference-name-audit.json"),
                                           "legacy_graph_reference_names_trusted": False}
    record = out / "receipt.json"
    record.write_text(json.dumps(report, indent=2) + "\n")
    with (out / "map.log").open("w") as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True)
    command = [sys.executable, str(source / "scripts/plan-pong-fileset18e.py"),
               "--unixnu", str(out / "kit-root"), "--original", str(original), "--native", str(native),
               "--map", str(out / "pairing.json"), "--out", str(out / "imports.json"), "--imports-only"]
    report["commands"].append(command)
    with (out / "plan.log").open("w") as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True)
    imports = json.loads((out / "imports.json").read_text())
    # A candidate graph can be incomplete and still contain wrong identities.
    # Always derive a separate proof-closed map/import inventory before any
    # result can be considered for collection emission. Research tools may
    # keep the provisional graph, but it is explicitly not executable proof.
    candidate_pairing = json.loads((out / "pairing.json").read_text())
    candidate_pairing.update(qualification="PROVISIONAL_MATCHER_CANDIDATES_ONLY", bootable=False,
                             executable_imports_authorized=False)
    (out / "pairing.json").write_text(json.dumps(candidate_pairing, indent=2) + "\n")
    qualification_command = [sys.executable, str(ROOT / "scripts/native18-qualified-imports.py"),
                             "--prelink-kit", str(kit), "--stock-kernel", str(original),
                             "--native-kernel", str(native), "--pairing", str(out / "pairing.json"),
                             "--exact-names", str(reference_names), "--out", str(out / "qualified")]
    for parent in parents:
        qualification_command += ["--parent", str(parent.resolve())]
    for anchor in args.anchor:
        qualification_command += ["--anchor", str(anchor.resolve())]
    report["commands"].append(qualification_command)
    with (out / "qualification.log").open("w") as stream:
        subprocess.run(qualification_command, stdout=stream, stderr=subprocess.STDOUT, check=True)
    qualified = json.loads((out / "qualified/imports.json").read_text())
    report.update(finished_at=datetime.now().astimezone().isoformat(), missing_unique=imports["missing_unique"],
                  provisional_missing_unique=imports["missing_unique"],
                  qualified_missing_unique=qualified["missing_unique"],
                  mapping_complete=qualified["mapping_complete"],
                  page_geometry_bootstrap_identity_qualified=qualified["page_geometry_bootstrap_identity_qualified"],
                  page_geometry_qualified=qualified["page_geometry_qualified"],
                  result="QUALIFIED_IMPORTS_MATCHED_LAYOUT_UNCHECKED" if qualified["mapping_complete"] else "QUALIFIED_IMPORTS_INCOMPLETE")
    record.write_text(json.dumps(report, indent=2) + "\n")
    print("Native closure analysis:", report["result"], "qualified missing", report["qualified_missing_unique"],
          "; provisional graph missing", report["provisional_missing_unique"], flush=True)


if __name__ == "__main__":
    main()
