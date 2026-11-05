"""Stage 6: план, отчёт, восстановление причин и отказ без изменения кэша."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

PROJECT = Path(__file__).resolve().parents[1]


def run(args, cwd, ok=True):
    p = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    if ok and p.returncode != 0:
        raise AssertionError(f"Failed {args}: {p.returncode}\n{p.stdout}\n{p.stderr}")
    return p


def main():
    with tempfile.TemporaryDirectory(prefix="pgr_stage6_") as temp:
        root = Path(temp)
        source = root / "Демо проект"
        source.mkdir()
        (source / "params.h").write_text("#define NUMBER 42\n", encoding="utf-8")
        (source / "main.c").write_text('#include "params.h"\nint main(void) { return NUMBER == 42 ? 0 : 1; }\n')
        build = root / "out dir"
        harness = root / "harness.cpp"
        harness.write_text('''
#include "core/gcc/gcc.hpp"
int main(int argc, char** argv) {
  project_t project(argv[1]);
  if (project.scan() != 0) return 2;
  gcc_t gcc;
  gcc.set_build_dir(argv[2]);
  if (argc >= 4 && std::string(argv[3]) == "PLAN") return gcc.preview(project);
  if (argc >= 4 && std::string(argv[3]) == "O2") {
    auto settings = gcc_t::build_settings_t{};
    settings.c.optimization = gcc_t::optimization_t::O2;
    gcc.set_build_settings(settings);
  }
  return gcc.run(project);
}
''', encoding="utf-8")
        exe = root / ("harness.exe" if os.name == "nt" else "harness")
        run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
             "-I", str(PROJECT), str(harness), "-o", str(exe)], root)
        project_build = build / source.name
        args = [str(exe), str(source), str(build)]

        # Preview не создаёт даже директории build.
        preview = run(args + ["PLAN"], root)
        assert not build.exists(), "preview must not create build directory"
        assert "[PLAN] PREPROCESSING" in preview.stdout
        assert "FIRST_BUILD" in preview.stdout
        print("[PASS] dry-run is read-only")

        run(args, root)
        report_path = project_build / "reports" / "build-report.json"
        manifest_path = project_build / "build-manifest.json"
        commands_path = project_build / "compile_commands.json"
        report = json.loads(report_path.read_text(encoding="utf-8"))
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        database = json.loads(commands_path.read_text(encoding="utf-8"))
        assert report["exit_code"] == 0
        assert [x["stage"] for x in report["operations"]][:4] == [
            "PREPROCESSING", "COMPILATION", "ASSEMBLY", "LINKING"
        ]
        assert all(x["status"] == "EXECUTED" for x in report["operations"][:4])
        assert all(x["reason"] != "UP_TO_DATE" for x in report["operations"][:4])
        assert all(x["arguments"] for x in report["operations"][:4])
        assert all(x["duration_ms"] >= 0 for x in report["operations"])
        assert "--" not in database[0]["arguments"]
        assert database[0]["file"].endswith("main.c")
        assert manifest["tools"] and manifest["tools"][0]["version"] != "unavailable"
        assert any("params.h" in dep["path"] for dep in manifest["sources"][0]["dependencies"])
        print("[PASS] four stages / sources / tool versions / compilation database")

        # No change should be reflected in all four skipped operations.
        run(args, root)
        report = json.loads(report_path.read_text(encoding="utf-8"))
        assert [x["status"] for x in report["operations"][:4]] == ["SKIPPED"] * 4
        assert all(x["reason"] == "UP_TO_DATE" for x in report["operations"][:4])
        assert all(x["duration_ms"] == 0 for x in report["operations"][:4])
        print("[PASS] unchanged build is fully skipped")

        # Header change should invalidate C and linker, and dry-run stays read-only.
        (source / "params.h").write_text("#define NUMBER 43\n", encoding="utf-8")
        report_old = report_path.read_bytes()
        preview = run(args + ["PLAN"], root)
        assert "INPUTS_CHANGED" in preview.stdout
        assert "OBJECT_UPDATED" in preview.stdout
        assert report_path.read_bytes() == report_old
        run(args, root)
        report = json.loads(report_path.read_text(encoding="utf-8"))
        assert report["operations"][0]["reason"] == "INPUTS_CHANGED"
        assert next(x for x in report["operations"] if x["stage"] == "LINKING")["reason"] == "OBJECT_UPDATED"
        print("[PASS] header mutation explains rebuild and link")

        # Compile error still writes a failed report.
        (source / "main.c").write_text('#error "forced failure"\n')
        failed = run(args, root, ok=False)
        assert failed.returncode != 0
        report = json.loads(report_path.read_text(encoding="utf-8"))
        assert report["exit_code"] != 0
        assert any(x["status"] == "FAILED" for x in report["operations"])
        print("[PASS] unsuccessful build is recorded")

        # c++ flags cause a preview without modifying caches, on a clean file.
        (source / "main.c").write_text('#include "params.h"\nint main(void) { return NUMBER == 43 ? 0 : 1; }\n')
        run(args, root)
        preview = run(args + ["PLAN"], root)
        assert "[SKIP] COMPILATION" in preview.stdout
        preview = run(args + ["O2"], root)
        report = json.loads(report_path.read_text(encoding="utf-8"))
        assert report["operations"][0]["reason"] == "COMMAND_CHANGED"
        print("[PASS] optimization change explains command invalidation")


if __name__ == "__main__":
    main()
