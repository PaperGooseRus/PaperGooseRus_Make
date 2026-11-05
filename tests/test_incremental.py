"""Регрессионная проверка инкрементальной сборки PGR Make (Python 3 + GCC)."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

PROJECT = Path(__file__).resolve().parents[1]


def check(actual, expected, title):
    assert actual == expected, f"{title}: expected {expected}, got {actual}"
    print("[PASS]", title, actual)


def main():
    with tempfile.TemporaryDirectory(prefix="pgr_incremental_") as temp:
        root = Path(temp)
        source = root / "demo project"
        build = root / "out build"
        source.mkdir()
        (source / "nested.h").write_text("#define BASE 40\n")
        (source / "value settings.h").write_text('#include "nested.h"\n#define VALUE BASE\n')
        (source / "other.h").write_text("#define OTHER 2\n")
        (source / "answer.c").write_text(
            '#include "value settings.h"\nint answer(void) {return VALUE;}\n'
        )
        (source / "helper.c").write_text(
            '#include "other.h"\nint helper(void) {return OTHER;}\n'
        )
        (source / "main.cpp").write_text(
            'extern "C" int answer(void);\n'
            'extern "C" int helper(void);\n'
            'int main(){return answer() + helper() == 42 ? 0 : 1;}\n'
        )

        harness = root / "harness.cpp"
        harness.write_text(
            '#include "core/gcc/gcc.hpp"\n'
            'int main(int argc, char* argv[]) {\n'
            '  if (argc < 3) return 1;\n'
            '  project_t project(argv[1]);\n'
            '  if (project.scan() != 0) return 2;\n'
            '  gcc_t gcc;\n'
            '  gcc.set_build_dir(argv[2]);\n'
            '  if (argc > 3) {\n'
            '    gcc_t::build_settings_t options;\n'
            '    if (std::string(argv[3]) == "LINK")\n'
            '      options.linking_flags.push_back("-Wl,-Map=link.map");\n'
            '    if (std::string(argv[3]) == "C_OPTIONS")\n'
            '      options.c.optimization = gcc_t::optimization_t::O2;\n'
            '    gcc.set_build_settings(options);\n'
            '  }\n'
            '  return gcc.run(project);\n'
            '}\n'
        )
        harness_exe = root / ("harness.exe" if os.name == "nt" else "harness")
        subprocess.run(
            ["g++", "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
             "-I", str(PROJECT), str(harness), "-o", str(harness_exe)],
            check=True, cwd=root,
        )

        def run(mode=""):
            args = [str(harness_exe), str(source), str(build)]
            if mode:
                args.append(mode)
            completed = subprocess.run(
                args, cwd=root, capture_output=True, text=True,
            )
            if completed.returncode:
                raise AssertionError(completed.stdout + completed.stderr)
            match = re.search(
                r"\[SUMMARY\] Rebuilt: (\d+) \| Skipped: (\d+) \| Linking: (YES|NO)",
                completed.stdout,
            )
            if not match:
                raise AssertionError("Missing summary:\n" + completed.stdout)
            return int(match[1]), int(match[2]), match[3]

        check(run(), (3, 0, "YES"), "initial build")
        check(run(), (0, 3, "NO"), "unchanged build")

        # Меняем один заголовок; обновляем время модификации позже объектного файла.
        header = source / "value settings.h"
        header.write_text('#include "nested.h"\n#define VALUE BASE /* updated */\n')
        check(run(), (1, 2, "YES"), "header with spaces")
        check(run(), (0, 3, "NO"), "repeat after header change")
        (source / "nested.h").write_text("#define BASE 40 /* nested change */\n")
        check(run(), (1, 2, "YES"), "transitive header dependency")
        check(run(), (0, 3, "NO"), "unchanged transitive dependency")

        check(run("LINK"), (0, 3, "YES"), "linker flags only")
        check(run("LINK"), (0, 3, "NO"), "unchanged linker flags")
        check(run("C_OPTIONS"), (2, 1, "YES"), "C compiler flags only")
        check(run("C_OPTIONS"), (0, 3, "NO"), "unchanged C flags")

        (source / "extra.cpp").write_text("int extra(){return 1;}\n")
        check(run("C_OPTIONS"), (1, 3, "YES"), "add source")
        (source / "extra.cpp").unlink()
        check(run("C_OPTIONS"), (0, 3, "YES"), "remove source")

        (build / "demo project" / "compilation" / "helper.c.s").unlink()
        check(run("C_OPTIONS"), (1, 2, "YES"), "missing intermediate file")

        executable = build / "demo project" / "linking" / "app.exe"
        subprocess.run([str(executable)], cwd=root, check=True)
        print("[PASS] compiled program execution")

        # Неудачная компиляция не должна помечать объект как успешно собранный.
        state = build / "demo project" / "cache" / "answer.c.cmd"
        old_state = state.read_bytes()
        original = (source / "answer.c").read_text()
        (source / "answer.c").write_text('#error "intentionally failed"\n')
        failed = subprocess.run(
            [str(harness_exe), str(source), str(build), "C_OPTIONS"],
            cwd=root, capture_output=True, text=True,
        )
        assert failed.returncode != 0, "compiler error was ignored"
        assert state.read_bytes() == old_state, "failed build rewrote cache"
        print("[PASS] failure does not update cache")
        (source / "answer.c").write_text(original)
        check(run("C_OPTIONS"), (1, 2, "YES"), "recover after compiler failure")


if __name__ == "__main__":
    main()
