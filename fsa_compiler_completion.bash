#!/bin/bash

_fsa_compiler_completions()
{
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    case "${prev}" in
        -p|--prefix)
            return 0
            ;;
    esac

    if [[ "${cur}" == -* ]]; then
        local opts="--help --prefix"
        COMPREPLY=( $(compgen -W "${opts}" -- "${cur}") )
        return 0
    fi
}

_fsa_bench_completions()
{
    local cur prev main_cmd
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    if [[ ${COMP_CWORD} -eq 1 ]]; then
        local subcommands="run search compile"
        COMPREPLY=( $(compgen -W "${subcommands}" -- "${cur}") )
        return 0
    fi

    main_cmd="${COMP_WORDS[1]}"

    case "${main_cmd}" in
        run)
            if [[ "${cur}" == -* ]]; then
                local run_opts="--seed --backend --help"
                COMPREPLY=( $(compgen -W "${run_opts}" -- "${cur}") )
            elif [[ "${prev}" == "run" ]]; then
                 COMPREPLY=( $(compgen -W "all" -- "${cur}") )
            fi
            ;;
        search|compile)
            if [[ "${cur}" == -* ]]; then
                COMPREPLY=( $(compgen -W "--help" -- "${cur}") )
            fi
            ;;
    esac
}

complete -o default -F _fsa_compiler_completions fsa_compiler
complete -o default -F _fsa_bench_completions fsa_bench