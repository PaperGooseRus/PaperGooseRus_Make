"""Автоматические проверки Stage 3. Требуются Python 3 и GCC/G++."""

from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "core/gcc/gcc.hpp"
#include "core/loger/loger.hpp"
#include <iostream>
int main(int argc, char* argv[]) {
    if (argc < 4) return 2;
    try {
        project_t project(argv[1]);
        if (project.scan() != 0) return 3;
        gcc_t gcc;
        gcc.set_build_dir(argv[2]);
        auto options = gcc.get_build_settings();
        options.c.optimization = gcc_t::optimization_t::O2;
        options.cpp.optimization = gcc_t::optimization_t::O2;
        const std::string mode = argv[3];
        if (mode == "SELECTED") options.analysis.mode = gcc_t::analysis_mode_t::SELECTED;
        else if (mode == "ALL") options.analysis.mode = gcc_t::analysis_mode_t::ALL;
        else if (mode == "NO_TIME") {
            options.analysis.mode = gcc_t::analysis_mode_t::SELECTED;
            options.analysis.time_report = false;
        } else if (mode == "NO_OPT") {
            options.analysis.mode = gcc_t::analysis_mode_t::SELECTED;
            options.analysis.optimization_report = false;
        }
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


def check(output, expected, title):
    summary = re.search(r'\[SUMMARY\] Rebuilt: (\d+) \| Skipped: (\d+) \| Linking: (YES|NO)', output)
    actual = (int(summary.group(1)), int(summary.group(2)), summary.group(3)) if summary else None
    assert actual == expected, f'{title}: expected {expected}, got {actual}\n{output[-5000:]}'
    print(f'[PASS] {title}: {actual}')


def main():
    with tempfile.TemporaryDirectory(prefix='PGR Stage3 ') as temp:
        base = Path(temp)
        root = base / 'source project'
        build = base / 'build projects'
        root.mkdir()
        (root / 'shared header.hpp').write_text('#define N 10\n')
        (root / 'math.c').write_text('int add(int a,int b){return a+b;}\n')
        (root / 'main.cpp').write_text(
            '#include "shared header.hpp"\n'
            'extern "C" int add(int,int);\n'
            'int f(int a){int s=0; for(int i=0;i<a;++i)s+=i;return s;}\n'
            'int main(){return add(f(N),0)==45?0:1;}\n'
        )
        harness = base / 'stage3_harness.cpp'
        harness.write_text(HARNESS)
        executable = base / 'harness.exe'
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Wpedantic','-Werror',
                        '-I',str(ROOT),str(harness),'-o',str(executable)],check=True)

        def run(mode, success=True):
            cp = subprocess.run([str(executable),str(root),str(build),mode],
                                capture_output=True,text=True)
            if success:
                assert cp.returncode == 0, f'Failed ({mode}) exit={cp.returncode}:\n{cp.stdout}\n{cp.stderr}'
            return cp

        report_dir = build / root.name / 'analysis'
        first = run('SELECTED')
        check(first.stdout, (2,0,'YES'), 'selected reports initially')
        for name in ('original.txt','gimple.txt','ssa.txt','optimized.txt',
                     'rtl-expand.txt','rtl-final.txt','optimizations.txt','timing.txt','analysis.ok'):
            path = report_dir / 'main.cpp' / name
            assert path.is_file(), f'Missing report: {path}'
        assert 'Time variable' in (report_dir / 'main.cpp' / 'timing.txt').read_text(), 'No timing data'
        assert 'Time variable' in (build / root.name / f'{root.name}.log').read_text(), 'Timing not replayed to log'
        assert (report_dir / 'math.c' / 'gimple.txt').is_file(), 'C report missing'
        check(run('SELECTED').stdout,(0,2,'NO'),'selected incremental no changes')

        (report_dir / 'main.cpp' / 'ssa.txt').unlink()
        check(run('SELECTED').stdout,(1,1,'YES'),'deleted selected dump rebuilds only its unit')
        check(run('SELECTED').stdout,(0,2,'NO'),'selected settled after report recovery')

        check(run('ALL').stdout,(2,0,'YES'),'switch to full analysis')
        full_cpp = list((report_dir / 'main.cpp' / 'full').glob('*'))
        assert len(full_cpp)>10, f'Not enough GCC passes: {len(full_cpp)}'
        assert any('.gimple' in p.name for p in full_cpp), 'GIMPLE pass absent in full analysis'
        assert any('r.' in p.name for p in full_cpp), 'RTL passes absent in full analysis'
        check(run('ALL').stdout,(0,2,'NO'),'all passes incremental no changes')
        full_cpp[0].unlink()
        check(run('ALL').stdout,(1,1,'YES'),'missing full pass triggers regeneration')
        check(run('ALL').stdout,(0,2,'NO'),'full passes settled after regeneration')
        assert not (report_dir / 'main.cpp' / 'gimple.txt').exists(), 'Old selected dumps were left behind'

        (root / 'shared header.hpp').write_text('#define N 12\n')
        check(run('ALL').stdout,(1,1,'YES'),'header change with full analysis')
        check(run('NO_TIME').stdout,(2,0,'YES'),'disable timing report')
        assert not (report_dir / 'main.cpp' / 'timing.txt').exists(), 'Stale timing report remained'
        check(run('NO_TIME').stdout,(0,2,'NO'),'timing off incremental no changes')
        check(run('NO_OPT').stdout,(2,0,'YES'),'toggle optimization report')
        assert not (report_dir / 'main.cpp' / 'optimizations.txt').exists(), 'Stale optimization report remained'
        check(run('OFF').stdout,(2,0,'YES'),'turn analysis off')
        assert not (report_dir / 'main.cpp').exists(), 'Old reports remained with analysis OFF'
        check(run('OFF').stdout,(0,2,'NO'),'baseline build without analysis unchanged')

        # Ошибка компиляции должна попадать в журнал даже при перенаправлении stderr.
        (root / 'main.cpp').write_text('#error test_analysis_failure\n')
        fail=run('SELECTED',success=False)
        assert fail.returncode != 0, 'Compilation failure was swallowed'
        assert 'test_analysis_failure' in (build/root.name/f'{root.name}.log').read_text(), 'Diagnostics lost'
        (root / 'main.cpp').write_text('extern "C" int add(int,int);\nint main(){return add(1,2)==3?0:1;}\n')
        check(run('SELECTED').stdout,(2,0,'YES'),'recover from compiler failure after analysis mode change')
        subprocess.run([str(build/root.name/'linking'/'app.exe')],check=True)
        print('[PASS] runtime, GCC diagnostics and archived analysis outputs')

if __name__=='__main__':
    main()
