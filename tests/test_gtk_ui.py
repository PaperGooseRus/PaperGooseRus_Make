"""Проверки структуры header-only GTK4 и совместимости консольной сборки.

При наличии gtk4.pc дополнительно запускается настоящая компиляция GUI.
Запуск самого графического окна требует рабочего дисплея и выполняется вручную.
"""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    assert (ROOT / "gui/gtk_app.hpp").is_file()
    assert not (ROOT / "gui/gtk_app.cpp").exists()
    assert "class gtk_app_t final" in (ROOT / "gui/gtk_app.hpp").read_text(encoding="utf-8")
    assert "PGR_WITH_GTK4" in (ROOT / "main.cpp").read_text(encoding="utf-8")
    gui_source = (ROOT / "gui/gtk_app.hpp").read_text(encoding="utf-8")
    assert "gtk_css_provider" not in gui_source
    assert "gtk_widget_add_css_class" not in gui_source
    assert "GtkCssProvider" not in gui_source
    assert 'gtk_application_new("org.pgr.make", G_APPLICATION_NON_UNIQUE)' in gui_source
    assert 'gtk_application_new("org.pgr.make", G_APPLICATION_DEFAULT_FLAGS)' not in gui_source
    assert "-DPGR_WITH_GTK4" in gui_source
    assert 'project.exclude("gui/gtk_app.cpp")' in gui_source
    assert 'project.exclude("test")' in gui_source
    print("[PASS] Default GTK widgets, GTK4 self-build configuration and legacy source filter")

    cxx = shutil.which("g++")
    if not cxx:
        print("[SKIP] C++ compiler not found")
        return
    with tempfile.TemporaryDirectory(prefix="pgr_gtk_ui_") as scratch:
        binary = Path(scratch) / ("pgr_cli.exe" if os.name == "nt" else "pgr_cli")
        subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-pthread",
                        str(ROOT / "main.cpp"), "-o", str(binary)],
                       check=True, cwd=ROOT)
        result = subprocess.run([str(binary), str(ROOT)], input="0\n", text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                cwd=ROOT, timeout=20)
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert "PAPER GOOSE RUS MAKE" in result.stdout
        print("[PASS] CLI without GTK4")
        # Имитация макросов из Windows SDK в компиляции без GTK4.
        # STRICT не должен совпадать с именами полей/перечислений GCC.
        windows_macros = Path(scratch) / ("pgr_windows_macros.exe" if os.name == "nt" else "pgr_windows_macros")
        subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-pthread",
                        "-DSTRICT=1", "-DNOMINMAX=1", str(ROOT / "main.cpp"),
                        "-o", str(windows_macros)], check=True, cwd=ROOT)
        print("[PASS] Windows SDK macro names do not break gcc_t")
        pkg = shutil.which("pkg-config")
        if not pkg or subprocess.run([pkg, "--exists", "gtk4"]).returncode != 0:
            print("[SKIP] gtk4 development package not installed")
            return
        flags = subprocess.check_output([pkg, "--cflags", "gtk4"], text=True).split()
        libs = subprocess.check_output([pkg, "--libs", "gtk4"], text=True).split()
        gui = Path(scratch) / ("pgr_gui.exe" if os.name == "nt" else "pgr_gui")
        subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-pthread",
                        "-DPGR_WITH_GTK4", *flags, str(ROOT / "main.cpp"),
                        "-o", str(gui), *libs], check=True, cwd=ROOT)
        print("[PASS] GTK4 GUI compiled and linked")


if __name__ == "__main__":
    main()
