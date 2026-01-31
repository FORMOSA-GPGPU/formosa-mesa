#!/bin/bash

_fsa_compiler_completions()
{
    local cur prev main_cmd
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    
    main_cmd="${COMP_WORDS[1]}"

    local subcommands="bench compile"

    # First level
    if [[ ${COMP_CWORD} -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "${subcommands}" -- "${cur}") )
        return 0
    fi

    case "${main_cmd}" in
        bench)
            # Logic for 'mytool build ...'
            local build_opts="run search compile"
            COMPREPLY=( $(compgen -W "${build_opts}" -- "${cur}") )
            ;;
        *)
            # Unknown subcommand or 'help' - return empty or default
            ;;
    esac
}

complete -o default -F _fsa_compiler_completions fsa_compiler
