"""Stage 5: library tracking, input hashes, shell-free process, assembly, targets and profiles."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import textwrap

ROOT = Path(__file__).resolve().parents[1]


def run(args, *, input_text=None, cwd=None, expected=0):
    result = subprocess.run(args, input=input_text, text=True, capture_output=True, cwd=cwd)
    if result.returncode != expected:
        raise AssertionError(f"{args}: exit={result.returncode}, expected={expected}\n{result.stdout}\n{result.stderr}")
    return result.stdout + result.stderr


with tempfile.TemporaryDirectory(prefix="pgr_stage5_") as base:
    temp = Path(base)
    harness = temp / "harness.cpp"
    harness.write_text(textwrap.dedent("""
        #include <filesystem>
        #include <iostream>
        #include "core/gcc/gcc.hpp"
        int main(int argc, char** argv) {
          project_t project(argv[1]);
          if (project.scan() != 0) return 30;
          gcc_t gcc;
          gcc.set_build_dir(argv[2]);
          gcc_t::build_settings_t conf;
          if (argc > 3) {
            conf.library_dirs = {argv[3]};
            conf.libraries = {"version"};
          }
          gcc.set_build_settings(conf);
          return gcc.run(project);
        }
    """), encoding="utf-8")
    engine = temp / "engine"
    run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-I", str(ROOT), str(harness), "-o", str(engine)])
    src = temp / "uses_lib"
    src.mkdir()
    libs = temp / "libs"
    libs.mkdir()
    build = temp / "out"
    (src / "main.c").write_text("int version(void); int main(void) { return version(); }\n")
    def library(number):
        (libs / "version.c").write_text(f"int version(void) {{ return {number}; }}\n")
        run(["gcc", "-c", str(libs / "version.c"), "-o", str(libs / "version.o")])
        run(["ar", "rcs", str(libs / "libversion.a"), str(libs / "version.o")])
    library(1)
    out = run([str(engine), str(src), str(build), str(libs)])
    assert "Linking: YES" in out
    app = build / src.name / "linking" / "app.exe"
    run([str(app)], expected=1)
    out = run([str(engine), str(src), str(build), str(libs)])
    assert "Linking: NO" in out
    original = (libs / "libversion.a").stat()
    library(2)
    os.utime(libs / "libversion.a", ns=(original.st_atime_ns, original.st_mtime_ns))
    out = run([str(engine), str(src), str(build), str(libs)])
    assert "Rebuilt: 0" in out and "Linking: YES" in out, out
    run([str(app)], expected=2)
    print("[PASS] .a changed with restored mtime => relink only")

    original_source = (src / "main.c").stat()
    (src / "main.c").write_text("int version(void); int main(void) { return version() + 1; }\n")
    os.utime(src / "main.c", ns=(original_source.st_atime_ns, original_source.st_mtime_ns))
    out = run([str(engine), str(src), str(build), str(libs)])
    assert "Rebuilt: 1" in out, out
    run([str(app)], expected=3)
    print("[PASS] source content change with restored mtime => rebuild")

    evil = temp / "specials"
    evil.mkdir()
    (evil / "main$(touch pwned).c").write_text("int main(void) { return 0; }\n")
    out = run([str(engine), str(evil), str(build)], cwd=temp)
    assert "GCC BUILD SUCCESS" in out
    assert not (temp / "pwned").exists()
    print("[PASS] file name with shell expansion syntax is not executed")

    mixed = temp / "asm_project"
    mixed.mkdir()
    (mixed / "main.c").write_text("int asm_answer(void); int raw_answer(void); int main(void){ return asm_answer() + raw_answer() == 10 ? 0 : 1; }\n")
    (mixed / "inc.h").write_text("#define ANSWER 7\n")
    (mixed / "preprocessed.S").write_text('#include "inc.h"\n.globl asm_answer\nasm_answer:\n  movl $ANSWER, %eax\n  ret\n')
    (mixed / "raw.s").write_text('.globl raw_answer\nraw_answer:\n  movl $3, %eax\n  ret\n')
    out = run([str(engine), str(mixed), str(build)])
    assert "Rebuilt: 3" in out, out
    run([str(build / mixed.name / "linking" / "app.exe")])
    out = run([str(engine), str(mixed), str(build)])
    assert "Rebuilt: 0" in out and "Linking: NO" in out, out
    (mixed / "inc.h").write_text("#define ANSWER 8\n")
    out = run([str(engine), str(mixed), str(build)])
    assert "Rebuilt: 1" in out and "Linking: YES" in out, out
    print("[PASS] .s/.S assembly and header dependency are incremental")

    pgr = temp / "pgr_cli"
    run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", str(ROOT / "main.cpp"), "-o", str(pgr)])
    bad = temp / "bad"
    bad.mkdir()
    (bad / "main.c").write_text("int main(void) { return does_not_exist; }\n")
    out = run([str(pgr), str(bad)], input_text="1\n0\n", expected=1)
    assert "Build failed" in out
    print("[PASS] failed CLI build propagates nonzero final exit code")

    target = temp / "targets"
    target.mkdir()
    (target / "main.cpp").write_text('int main(){return 0;}\n')
    (target / "test.cpp").write_text('int main(){return 0;}\n')
    # CLI switches to separate named target and picks only main.cpp.
    script = '12\n1\nRelease\n0\n13\nmain.cpp\n0\n12\n2\n0\n1\n0\n'
    out = run([str(pgr), str(target)], input_text=script, cwd=temp)
    assert "Build successful" in out and "Linking: YES" in out, out
    cfg = target / '.pgr' / 'Release.cfg'
    assert cfg.exists()
    profile_main = temp / "profile_test.cpp"
    profile_main.write_text(textwrap.dedent('''
       #include "core/config/config.hpp"
       int main(int argc, char** argv) {
          config_t::profile_t profile;
          if (!config_t::load(argv[1], profile)) return 1;
          return profile.sources.size() == 1 && profile.sources[0] == "main.cpp" ? 0 : 2;
       }
    '''))
    run(["g++", "-std=c++17", "-I", str(ROOT), str(profile_main), "-o", str(temp / 'profile_test')])
    run([str(temp / 'profile_test'), str(cfg)])
    print("[PASS] selected target excludes second main and persists config")

    # Проверка отчёта по адресному размещению: .text не помещается в объявленную область.
    objdump = run(["objdump", "-h", str(app)])
    match = re.search(r"^\s*\d+\s+\.text\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", objdump, re.MULTILINE)
    assert match, objdump
    lma = int(match.group(3), 16)
    memory_helper = temp / "memory_helper.cpp"
    memory_helper.write_text(textwrap.dedent("""
       #include <cstdint>
       #include <string>
       #include "core/binary/binary.hpp"
       int main(int argc, char** argv) {
          binary_t binary;
          binary_t::settings_t config;
          config.reports = true;
          config.flash_origin = std::stoull(argv[4]);
          config.flash_size = 1;
          return binary.run(argv[1], argv[2], argv[3], config);
       }
    """), encoding="utf-8")
    run(["g++", "-std=c++17", "-I", str(ROOT), str(memory_helper), "-o", str(temp / "memory_helper")])
    reports = temp / "memory_reports"
    result = run([str(temp / "memory_helper"), str(app), str(reports), str(temp / "memory.cache"), str(lma)], expected=1)
    assert "OVERFLOW" in (reports / "memory.txt").read_text(), result
    print("[PASS] out-of-bounds Flash section is reported as an error")

print("[SUCCESS] Stage 5 regressions passed")
