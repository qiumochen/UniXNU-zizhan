"""Explicit zizhan 22H374 service selection, separate from runtime readiness."""
import hashlib
import json
from pathlib import Path
import re

FORMAT = "zizhan-22h374-service-policy-v1"


def validate(policy):
    if (not isinstance(policy, dict) or
            set(policy) != {"format", "name", "build", "excluded_jobs"} or
            policy["format"] != FORMAT or policy["build"] != "22H374" or
            not isinstance(policy["name"], str) or not policy["name"] or
            not isinstance(policy["excluded_jobs"], dict)):
        raise ValueError("Invalid zizhan 22H374 service policy")
    for label, reason in policy["excluded_jobs"].items():
        if (not isinstance(label, str) or not re.fullmatch(r"[A-Za-z0-9_.-]+", label) or
                not isinstance(reason, str) or not reason):
            raise ValueError("Service exclusions need a label and a nonempty reason")
    return policy


def resolve(path, defaults, previous=None):
    if path is not None:
        return validate(json.loads(Path(path).read_text()))
    if previous is not None:
        return validate(previous)
    return validate(dict(format=FORMAT, name="legacy-default-exclusions",
                         build="22H374", excluded_jobs=dict(defaults)))


def identity(policy):
    return hashlib.sha256(json.dumps(validate(policy), sort_keys=True,
                                     separators=(",", ":")).encode()).hexdigest()
