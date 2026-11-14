"""Project metadata stays consistent: pyproject.toml, uv.lock, requirements.txt, NOTICE.md."""

import re
from pathlib import Path

import pytest

import diskos_installer

tomllib = pytest.importorskip("tomllib")  # Python 3.11+; the 3.10 CI leg runs everything else

ROOT = Path(__file__).resolve().parents[1]
PROJECT = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))["project"]
LOCK = tomllib.loads((ROOT / "uv.lock").read_text(encoding="utf-8"))

# How NOTICE.md names each runtime dependency's version (pycryptodome: per-version files).
NOTICE_SPELLING = {
    "pyusb": "locked {version} wheel",
    "pycryptodome": "PyCryptodome-{version}-",
    "certifi": "certifi {version}",
    "platformdirs": "platformdirs {version}",
}


def runtime_pins():
    pins = {}
    for requirement in PROJECT["dependencies"]:
        name, version = requirement.split("==")
        pins[name] = version
    return pins


def test_version_matches_the_package():
    assert PROJECT["version"] == diskos_installer.__version__


def test_runtime_dependencies_are_exact_pins():
    assert all("==" in requirement for requirement in PROJECT["dependencies"])


@pytest.mark.parametrize("name", sorted(NOTICE_SPELLING))
def test_lock_requirements_and_notice_agree(name):
    version = runtime_pins()[name]
    locked = {package["name"]: package["version"] for package in LOCK["package"]}
    assert locked[name] == version

    requirements = (ROOT / "requirements.txt").read_text(encoding="utf-8")
    assert re.search(rf"^{re.escape(name)}=={re.escape(version)} ", requirements, re.MULTILINE)

    notice = (ROOT / "NOTICE.md").read_text(encoding="utf-8")
    assert NOTICE_SPELLING[name].format(version=version) in notice, (
        f"NOTICE.md does not name {name} {version}: update its license texts with the upgrade"
    )


def test_every_runtime_dependency_has_a_notice_entry():
    assert set(runtime_pins()) == set(NOTICE_SPELLING)
