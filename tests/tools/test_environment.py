"""Environment/download regressions; fixtures do not claim a real SystemC install."""
import importlib.util
import io
import tarfile
from pathlib import Path
from unittest.mock import patch

import pytest


def load_tool(name):
    path = Path(__file__).resolve().parents[2] / "tools" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def archive(path):
    with tarfile.open(path, "w:gz") as stream:
        member = tarfile.TarInfo("test.txt")
        member.size = 4
        stream.addfile(member, io.BytesIO(b"test"))


def test_doctor_checks_version_library_and_package(tmp_path):
    doctor = load_tool("esl_doctor")
    header = tmp_path / "include/sysc/kernel/sc_ver.h"
    header.parent.mkdir(parents=True)
    header.write_text("#define SC_VERSION_MAJOR 2\n#define SC_VERSION_MINOR 3\n#define SC_VERSION_PATCH 4\n")
    assert not doctor.check_systemc(tmp_path)[0]
    version = doctor.ENVIRONMENT["systemc_version"].split(".")
    header.write_text("".join(f"#define SC_VERSION_{key} {value}\n"
                              for key, value in zip(("MAJOR", "MINOR", "PATCH"), version, strict=True)))
    assert not doctor.check_systemc(tmp_path)[0]
    (tmp_path / "lib").mkdir()
    (tmp_path / "lib/libsystemc.so").write_text("fixture")
    assert not doctor.check_systemc(tmp_path)[0]
    (tmp_path / "lib/SystemCLanguageConfig.cmake").write_text("# fixture")
    assert doctor.check_systemc(tmp_path)[0]


def test_download_reuses_complete_archive_and_replaces_truncation(tmp_path):
    setup = load_tool("esl_env_setup")
    destination = tmp_path / "source.tar.gz"
    archive(destination)
    with patch.object(setup, "run", side_effect=AssertionError("unexpected download")):
        setup.ensure_url_download("fixture", destination)
    destination.write_bytes(destination.read_bytes()[:-8])

    def download(cmd, **kwargs):
        archive(Path(cmd[cmd.index("-o") + 1]))

    with patch.object(setup, "run", side_effect=download) as mocked:
        setup.ensure_url_download("fixture", destination)
        assert mocked.call_count == 1
    with tarfile.open(destination) as stream:
        assert stream.extractfile("test.txt").read() == b"test"
    assert not destination.with_suffix(".gz.part").exists()


def test_failed_download_does_not_promote_partial_archive(tmp_path):
    setup = load_tool("esl_env_setup")
    destination = tmp_path / "source.tar.gz"

    def download(cmd, **kwargs):
        Path(cmd[cmd.index("-o") + 1]).write_bytes(b"partial")
        raise RuntimeError("interrupted")

    with patch.object(setup, "run", side_effect=download):
        with pytest.raises(RuntimeError, match="interrupted"):
            setup.ensure_url_download("fixture", destination)
    assert not destination.exists()
    assert not destination.with_suffix(".gz.part").exists()
