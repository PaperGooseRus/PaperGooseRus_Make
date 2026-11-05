"""Автоматические проверки Stage 4: GNU Binutils, ELF/PE, BIN/HEX и Embedded-флаги."""

from pathlib import Path
import re
import subprocess
import tempfile
import time
import shutil

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "core/gcc/gcc.hpp"
#include "core/loger/loger.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc == 3 && std::string(argv[1]) == "FORMAT") {
        std::cout << binary_t::format_name(binary_t::detect_format(argv[2]));
        return 0;
    }
    if (argc == 5 && std::string(argv[1]) == "PE_REPORT") {
        binary_t binary;
        binary_t::settings_t cfg;
        cfg.reports = true;
        return binary.run(argv[2], argv[3], argv[4], cfg);
    }
    if (argc < 4) return 2;
    try {
        project_t project(argv[1]);
        if (project.scan() != 0) return 3;
        gcc_t gcc;
        gcc.set_build_dir(argv[2]);
        auto options = gcc.get_build_settings();
        const std::string mode = argv[3];
        options.binary.reports = mode != "OFF" && mode != "NO_REPORTS";
        options.binary.linker_map = mode != "OFF" && mode != "NOMAP";
        options.binary.generate_bin = mode != "OFF" && mode != "NOBIN";
        options.binary.generate_hex = mode != "OFF" && mode != "NOHEX";
        if (mode == "NO_REPORTS") {
            options.binary.generate_bin = true;
            options.binary.generate_hex = true;
        }
        if (mode == "BADTOOL") options.binary.toolchain_prefix = "pgr-tool-that-does-not-exist-";
        if (mode == "SCRIPT") {
            if (argc < 5) return 5;
            options.linker_script = argv[4];
        }
        if (mode == "ARCH") options.target_flags = {"-m64"};
        gcc.set_build_settings(options);
        loger_t loger(project.name(), argv[2]);
        gcc.set_command_executor([&loger](const std::string& command) {
            return loger.execute(command);
        });
        return gcc.run(project);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 4;
    }
}
'''


def check(output, expected, name):
    summary = re.search(r'\[SUMMARY\] Rebuilt: (\d+) \| Skipped: (\d+) \| Linking: (YES|NO)', output)
    actual = (int(summary.group(1)), int(summary.group(2)), summary.group(3)) if summary else None
    assert actual == expected, f'{name}: expected {expected}, got {actual}\n{output[-6000:]}'
    print(f'[PASS] {name}: {actual}')


def main():
    with tempfile.TemporaryDirectory(prefix='PGR Stage4 ') as temporary:
        base = Path(temporary)
        source = base / 'embedded source'
        build = base / 'build path with spaces'
        source.mkdir()
        (source / 'main.cpp').write_text('extern "C" int arithmetic(int);\nint main(){return arithmetic(6)==42?0:1;}\n')
        (source / 'arithmetic.c').write_text('int arithmetic(int x){return x*7;}\n')
        (base / 'harness.cpp').write_text(HARNESS)
        executable = base / 'harness.exe'
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                        '-I', str(ROOT), str(base / 'harness.cpp'), '-o', str(executable)], check=True)

        def run(mode='FULL', expect_success=True, extra=None):
            args = [str(executable), str(source), str(build), mode]
            if extra is not None:
                args.append(str(extra))
            cp = subprocess.run(args, capture_output=True, text=True)
            if expect_success:
                assert cp.returncode == 0, f'Build failed mode={mode} rc={cp.returncode}:\n{cp.stdout}\n{cp.stderr}'
            return cp

        project_dir = build / source.name
        reports = project_dir / 'binary'
        linker = project_dir / 'linking'
        check(run().stdout, (2, 0, 'YES'), 'full first build')
        for name in ('format.txt', 'file-headers.txt', 'sections.txt', 'symbols.txt',
                     'disassembly.txt', 'size.txt', 'size-summary.txt', 'program-headers.txt',
                     'app.bin', 'app.hex', 'manifest.txt'):
            assert (reports / name).is_file(), f'Missing binary artifact: {name}'
        assert (linker / 'app.map').is_file(), 'Linker map absent'
        assert 'Format: ELF' in (reports / 'format.txt').read_text(), 'Incorrect ELF format'
        assert '.text' in (reports / 'sections.txt').read_text(), 'Missing .text section'
        assert 'arithmetic' in (reports / 'symbols.txt').read_text(), 'Missing arithmetic symbol'
        assert (reports / 'app.bin').stat().st_size > 0, 'Empty BIN image'
        assert (reports / 'app.hex').read_text().startswith(':'), 'Invalid Intel HEX syntax'
        subprocess.run([str(linker / 'app.exe')], check=True)
        print('[PASS] ELF, link map, sections, symbols, images, execution')
        check(run().stdout, (0, 2, 'NO'), 'no-change build')
        assert '[BINARY] UP-TO-DATE' in run().stdout, 'Analysis was repeated without changes'

        (reports / 'symbols.txt').unlink()
        result = run().stdout
        check(result, (0, 2, 'NO'), 'deleted report, no GCC rebuild')
        assert '[BINARY REPORT]' in result, 'Deleted report not restored'
        check(run('NOHEX').stdout, (0, 2, 'NO'), 'change report mode, no recompilation')
        assert not (reports / 'app.hex').exists(), 'Stale HEX was not removed'
        check(run('NO_REPORTS').stdout, (0, 2, 'NO'), 'BIN/HEX only')
        assert (reports / 'app.bin').is_file() and not (reports / 'sections.txt').exists()
        check(run('FULL').stdout, (0, 2, 'NO'), 'restore report settings')

        check(run('NOMAP').stdout, (0, 2, 'YES'), 'disable linker map triggers link only')
        assert not (linker / 'app.map').exists(), 'Obsolete linker map was kept after disabling it'
        check(run('FULL').stdout, (0, 2, 'YES'), 'enable linker map triggers link only')
        check(run('ARCH').stdout, (2, 0, 'YES'), 'architecture flags trigger stage recompilation')
        check(run('ARCH').stdout, (0, 2, 'NO'), 'architecture settings cache')

        script = base / 'my linker script.ld'
        script.write_text('SECTIONS { .pgrnote : { *(.pgrnote) } } INSERT AFTER .text;\n')
        check(run('SCRIPT', extra=script).stdout, (2, 0, 'YES'), 'switch architecture flags back and add script')
        check(run('SCRIPT', extra=script).stdout, (0, 2, 'NO'), 'script up to date')
        time.sleep(0.03)
        script.write_text('SECTIONS { .pgrnote : { *(.pgrnote) } } INSERT AFTER .text;\n/* changed */\n')
        check(run('SCRIPT', extra=script).stdout, (0, 2, 'YES'), 'changed script relinks without compilation')
        check(run('SCRIPT', extra=script).stdout, (0, 2, 'NO'), 'script cache after relink')
        (linker / 'app.map').unlink()
        check(run('SCRIPT', extra=script).stdout, (0, 2, 'YES'), 'missing map forces relink')

        failure = run('BADTOOL', expect_success=False)
        assert failure.returncode != 0, 'Unknown Binutils tool silently ignored'
        assert 'BINARY ERROR' in failure.stdout + failure.stderr, 'No Binutils diagnostic'
        check(run('FULL').stdout, (0, 2, 'NO'), 'recover from unsupported toolchain without relinking')
        check(run('OFF').stdout, (0, 2, 'YES'), 'disable post-link analysis and map')
        assert not reports.exists(), 'Stale reports remain when disabled'
        check(run('OFF').stdout, (0, 2, 'NO'), 'default baseline stable')

        elf = subprocess.run([str(executable), 'FORMAT', str(linker / 'app.exe')],
                             capture_output=True, text=True, check=True)
        assert elf.stdout == 'ELF', 'ELF detection failed'
        pe = base / 'sample.exe'
        payload = bytearray(512)
        payload[0:2] = b'MZ'
        payload[0x3c:0x40] = (0x80).to_bytes(4, 'little')
        payload[0x80:0x84] = b'PE\0\0'
        pe.write_bytes(payload)
        detected = subprocess.run([str(executable), 'FORMAT', str(pe)],
                                  capture_output=True, text=True, check=True)
        assert detected.stdout == 'PE/COFF', 'PE detection failed'
        pe.write_bytes(b'not-an-executable')
        detected = subprocess.run([str(executable), 'FORMAT', str(pe)],
                                  capture_output=True, text=True, check=True)
        assert detected.stdout == 'UNKNOWN', 'Unsupported executable detection failed'
        print('[PASS] ELF/PE recognition, no extension-based guessing')

        # Реальный PE/COFF без CRT через LLVM, если clang и lld-link доступны.
        if shutil.which('clang') and shutil.which('lld-link'):
            source_pe = base / 'pe_sample.c'
            source_pe.write_text('int main(void) { volatile int value = 5; return value - 5; }\n')
            pe_obj = base / 'pe_sample.obj'
            pe_exe = base / 'pe_sample.exe'
            pe_dir = base / 'PE reports'
            subprocess.run(['clang', '--target=x86_64-pc-windows-msvc', '-c',
                            str(source_pe), '-o', str(pe_obj)], check=True)
            subprocess.run(['lld-link', '/entry:main', '/subsystem:console', '/nodefaultlib',
                            '/out:' + str(pe_exe), str(pe_obj)], check=True)
            real_pe = subprocess.run([str(executable), 'PE_REPORT', str(pe_exe), str(pe_dir),
                                      str(base / 'pe_cache.cmd')], capture_output=True, text=True)
            assert real_pe.returncode == 0, f'PE/COFF analysis failed:\n{real_pe.stdout}\n{real_pe.stderr}'
            assert 'Format: PE/COFF' in (pe_dir / 'format.txt').read_text()
            assert (pe_dir / 'pe-headers.txt').is_file(), 'PE headers not reported'
            assert (pe_dir / 'sections.txt').is_file(), 'PE sections not reported'
            assert not (pe_dir / 'program-headers.txt').exists(), 'readelf applied to PE'
            print('[PASS] Real PE/COFF report extraction with GNU Binutils')
        else:
            print('[SKIP] Real PE/COFF analysis: clang/lld-link not installed')

        # Полный CLI: меню создаёт отчёты, затем позволяет их открыть и очистить.
        cli = base / 'pgr-cli.exe'
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Wpedantic','-Werror',
                        str(ROOT / 'main.cpp'), '-o', str(cli)], check=True)
        # Настройки CLI действуют только в текущем процессе; создаём отдельный проект со своим именем.
        cli_run = subprocess.run([str(cli), str(source)], input='11\n1\n2\n3\n4\n0\n1\n11\n8\n0\n0\n0\n',
                                 capture_output=True, text=True, timeout=45)
        # CLI использует C:/PGR/build по умолчанию; в Linux этот относительный путь допустим.
        assert cli_run.returncode == 0, f'CLI failed:\n{cli_run.stdout[-2500:]}\n{cli_run.stderr}'
        assert '[EXECUTABLE / EMBEDDED]' in cli_run.stdout, 'Embedded menu not shown'
        assert '[BINARY REPORT]' in cli_run.stdout, f'CLI build did not create reports:\n{cli_run.stdout[-2000:]}'
        print('[PASS] CLI workflow, interactive report browsing')
        print('[SUCCESS] Stage 4 tested successfully')


if __name__ == '__main__':
    main()
