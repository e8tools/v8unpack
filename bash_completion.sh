_v8unpack_complete()
{
	local cur opts
	COMPREPLY=()
	cur="${COMP_WORDS[COMP_CWORD]}"
	prev="${COMP_WORDS[COMP_CWORD]}"
	opts="-unpack -pack -parse -build -inflate -deflate \
		-list -example -bat -version"

	if [[ ${COMP_CWORD} == 1 ]] ; then
		COMPREPLY=( $(compgen -W "$opts" -- ${cur}) )
		return 0
	fi

	COMPREPLY=( $(compgen -f ${cur}) )
	return 0
}
complete -F _v8unpack_complete v8unpack

