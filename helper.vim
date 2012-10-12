" vim: foldmarker=<([{,}])> foldmethod=marker

" Temporary varaibles are used anywhere.
let s:str = ''
let s:i = 0
let s:alist = []

" Tool functions <([{
" Get filename and identifier.
function! s:noInput()
let s:file_name = fnamemodify(bufname('%'), '')
let s:symbol = expand('<cword>')
endfunction

let s:jump_history = []
" Jump to the tag.
function! s:jumpTo()
let s:file_name = get(s:alist, 0)
let s:file_offset = get(s:alist, 1) + 1
call add(s:jump_history, fnamemodify(bufname('%'), ''))
call add(s:jump_history, col('.') + line2byte(line('.')) - 1)
execute 'ed +go\ ' . s:file_offset . ' ' . s:file_name
endfunction

" Jump back to the previous tag.
function! s:jumpBack()
let s:i = len(s:jump_history)
if s:i == 0
	return
endif
let s:alist = remove(s:jump_history, -2, -1)
let s:file_name = s:alist[0]
let s:file_offset = s:alist[1]
execute 'ed +go\ ' . s:file_offset . ' ' . s:file_name
endfunction

" Show multiple selection and get user-input.
function! s:showSelection()
let s:alist = split(s:str, '\n')
let s:selection = len(s:alist)
if s:selection != 1
	echo 'GS multiple definition:'
	let s:i = 1
	for s:element in s:alist
		echo s:i . ')' . s:element
		let s:i += 1
	endfor
	let s:selection = str2nr(input('? '))
	if s:selection <= 0 || s:selection > len(s:alist)
		return -1
	endif
endif
let s:str = get(s:alist, s:selection - 1)
let s:alist = split(s:str)
endfunction

" }])>

" GS_ifdef <([{
function! s:GS_ifdef()
call s:noInput()
let s:i = col('.') + line2byte(line('.')) - 1
let s:str = system('./gs ifdef ' . s:file_name . ' ' . s:i)
echo s:str
endfunction
" }])>

" GS_def <([{
function! s:GS_def(from)
if a:from != 1
	call s:noInput()
endif

" Call gs
let s:str = system('./gs def ' . s:file_name . ' ' . s:symbol)
if s:str == ''
	let s:str = system('./gs def -- ' . s:symbol)
endif
if s:str == ''
	return
endif

let s:i = s:showSelection()
if s:i == -1
	return
endif
call s:jumpTo()
endfunction
" }])>

" GS_callee <([{
function! s:GS_callee(from)
if a:from != 1
	call s:noInput()
endif

" Call gs
let s:str = system('./gs callee -- ' . s:symbol)
let s:str2 = system('./gs falias fundecl ' . s:symbol)
let s:alist = split(s:str2, '\n')
for s:element in s:alist
	let s:str = s:str . s:element . " CALL_MEMBER_POINTER <<< \n"
	let s:blist = split(s:element)
	let s:str = s:str . system('./gs callee -- ' . s:blist[3])
endfor
if s:str == ''
	return
endif

let s:i = s:showSelection()
if s:i == -1
	return
endif
call s:jumpTo()
endfunction
" }])>

" GS_jumpback <([{
function! s:GS_jumpback()
call s:jumpBack()
endfunction
" }])>

" GS_falias <([{
function! s:GS_falias_def()
let s:str = ''
let s:str2 = system('./gs falias member ' . s:symbol)
let s:alist = split(s:str2, '\n')
for s:element in s:alist
	let s:str = s:str . s:element . " MEMBER_POINTER <<< \n"
	let s:blist = split(s:element)
	let s:str = s:str . system('./gs def -- ' . s:blist[3])
endfor
if s:str == ''
	return
endif

let s:i = s:showSelection()
if s:i == -1
	return
endif
call s:jumpTo()
endfunction

function! s:GS_falias()
let s:str = system('./gs falias member ' . s:symbol)
echo s:str
let s:str = system('./gs falias fundecl ' . s:symbol)
echo s:str
endfunction
" }])>

" GS_cmd <([{
" Support only def/callee subcommand.
function! s:GS_cmd(subcmd, ...)
if a:subcmd == 'def'
	let s:file_name = '--'
	let s:symbol = a:1
	call s:GS_def(1)
elseif a:subcmd == 'callee'
	let s:file_name = '--'
	let s:symbol = a:1
	call s:GS_callee(1)
elseif a:subcmd == 'ifdef'
	call s:GS_ifdef()
elseif a:subcmd == 'falias'
	let s:file_name = '--'
	let s:symbol = a:1
	call s:GS_falias()
elseif a:subcmd == 'fdef'
	let s:file_name = '--'
	let s:symbol = a:1
	call s:GS_falias_def()
endif
endfunction
" }])>

" Note:
" To walk through file by file-offset, try `:go file-offset'.
" To see where char-offset is from file, try `g<CTRL-g>' on the char.

nmap <C-]> :call <SID>GS_def(0)<CR>
nmap <C-[> :call <SID>GS_callee(0)<CR>
nmap <C-T> :call <SID>GS_jumpback()<CR>

" To `Gs' command of vim, filename is always `--'.
command! -nargs=* Gs  call <SID>GS_cmd(<f-args>)
