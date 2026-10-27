# **Cppcheck MISRA C / clang-format / Flawfinder** 

Docker image based on alpine including 

* cppcheck with MISRA C 2023 rules
* flawfinder
* clang-format

## Build docker

```
    docker build --target runtime -t <imagename> .
```

The `runtime` target includes CMake and the C toolchain so CI can generate compilation
databases with analysis-specific configuration flags. The repository's
`.devcontainer/devcontainer.json` uses the `development` target, which additionally includes
Bash, Git, and Ninja.

## Cppcheck MISRA usage

Launch a container based on this image

1. Dump source files
```
    cppcheck --dump *.c
```

2. Analyze with MISRA rules
```
    python3 /usr/local/share/Cppcheck/addons/misra.py --rule-texts=misra2023_rules_ascii.txt *.c.dump
```

* Ignore MISRA rules

```
    python3 /usr/local/share/Cppcheck/addons/misra.py --rule-texts=misra2023_rules_ascii.txt --suppress-rules 17.3,21.12 *.c.dump
```

* Save report in JUnit format

```
    python3 /usr/local/share/Cppcheck/addons/misra.py --rule-texts=misra2023_rules_ascii.txt --junit-file-export=report.xml *.c.dump
```

## Flawfinder

* Launch a container based on this image
* Analyze 
```
    flawfinder -C -i *
```

HTML export is possible
```
    flawfinder -C --html -i * > ./flawfinder_report.html
```

## Roadmap

* JUnit export for easy CI integration
