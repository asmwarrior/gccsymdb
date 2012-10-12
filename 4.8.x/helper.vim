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
let s:line = get(s:alist, 1)
let s:column = get(s:alist, 2)
call add(s:jump_history, fnamemodify(bufname('%'), ''))
let s:jump_history = s:jump_history + getpos('.')[1:2]
execute 'ed +call\ setpos(".",[0,' . s:line . ',' . s:column . ',0]) ' . s:file_name
endfunction

" Jump back to the previous tag.
function! s:jumpBack()
let s:i = len(s:jump_history)
if s:i == 0
	return
endif
let s:alist = remove(s:jump_history, -3, -1)
let s:file_name = s:alist[0]
let s:line = s:alist[1]
let s:column = s:alist[2]
execute 'ed +call\ setpos(".",[0,' . s:line . ',' . s:column . ',0]) ' . s:file_name
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

" GS_def <([{
function! s:GS_def(from)
if a:from != 1
	call s:noInput()
endif

" Call gs
let s:str = system('./gs def ' . s:file_name . ' ' . s:symbol)
if s:str == ''
	let s:str = system('./gs def -- ' . s:symbol)
	if s:str == ''
		return
	endif
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

" GS_cmd <([{
" Support only def/callee subcommand.
function! s:GS_cmd(subcmd, symbol)
if a:subcmd == 'def'
	let s:file_name = '--'
	let s:symbol = a:symbol
	call s:GS_def(1)
elseif a:subcmd == 'callee'
	let s:file_name = '--'
	let s:symbol = a:symbol
	call s:GS_callee(1)
endif
endfunction
" }])>

nmap <C-]> :call <SID>GS_def(0)<CR>
nmap <C-[> :call <SID>GS_callee(0)<CR>
nmap <C-T> :call <SID>GS_jumpback()<CR>

" Gs command of vim only supports def/callee subcommands, and filename is
" always `--'.
command! -nargs=* Gs  call <SID>GS_cmd(<f-args>)
