# PaperGooseRus Make

Проект сохранён в исходной структуре архива. Реализация `project_t` и `gcc_t` полностью размещена в `.hpp`.

## Компоненты

- `core/project/project.hpp`: `project_t` сканирует исходники `.c`, `.cpp`, `.cc`, `.cxx`, `.C`, исключая `.git`, `.vscode`, `build`, `node_modules`. Возвращает `sources()`, `include_dirs()`, `root()`, `name()`.
- `core/gcc/gcc.hpp`: `gcc_t` выполняет отдельно `preprocessing()`, `compilation()`, `assembly()`, `linking()`, управляет параметрами C/C++ и компоновкой всех объектных файлов.
- `main.cpp`: сохранён без изменений, вызывает `gcc.run(argc, argv)`.

## Сборка управляющего приложения

В MSYS2 UCRT64 (из корня проекта):

```sh
g++ -std=c++17 -Wall -Wextra -pedantic main.cpp -o pgr_make.exe
./pgr_make.exe test
```

Или передать каталог другого проекта: `./pgr_make.exe C:/Projects/demo`.

По умолчанию результаты создаются в `C:/PGR/build/<имя проекта>/`.

## API

```cpp
project_t project("C:/Projects/demo");
if (project.scan() != 0) return 1;
project.add_include_dir("headers");

gcc_t gcc;
gcc.set_build_dir("C:/PGR/build");
gcc.set_language_settings({gcc_t::language_t::C, gcc_t::standart_t::C17, gcc_t::optimization_t::O0, {"-Wall"}});
gcc.set_language_settings({gcc_t::language_t::CPP, gcc_t::standart_t::CPP17, gcc_t::optimization_t::O0, {"-Wall"}});
return gcc.run(project);
```

## Ограничения

- Проект ориентирован на один исполняемый файл, сборка полная (не инкрементальная).
- `std::system()` и флаги командной строки рассчитаны на доверенные локальные пути и параметры. Для произвольного ввода нужен запуск без shell.
- Пути исходников должны находиться внутри корня проекта; символьные ссылки и внешние исходники не обрабатываются специально.
- Для нестандартных библиотек нужны дополнительные флаги линковки.
- `gcc_compilation_step_t` оставлен для будущей диагностики; текущая реализация не управляет внутренними проходами GCC.
- Неизменённые `gui` и `core/loger` не входят в тестовую сборку.
