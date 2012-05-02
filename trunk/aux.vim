" vim: foldmarker=<([{,}])> foldmethod=marker

" Temporary varaibles are used anywhere.
let s:str = ''
let s:i = 0
let s:alist = []

" Tool functions <([{
" Get filename and identifier.
function! s:initInput()
let s:file_name = fnamemodify(bufname('%'), '')
let s:symbol = expand('<cword>')
endfunction

" Jump to the tag.
function! s:jumpTo()
let s:file_name = get(s:alist, 0)
let s:file_offset = get(s:alist, 1) + 1
execute 'ed +go\ ' . s:file_offset. ' ' . s:file_name
endfunction

" Show multiple selection and get user-input.
function! s:showSelection()
let s:alist = split(s:str, '\n')
let s:selection = len(s:alist)
if s:selection != 1
	echo 'Multiple definition:'
	let s:i = 1
	for s:element in s:alist
		echo s:i . ')' . s:element
		let s:i += 1
	endfor
	let s:selection = str2nr(input('? '))
	if s:selection <= 0 || s:selection > len(s:alist)
		finish
	endif
endif
let s:str = get(s:alist, s:selection - 1)
let s:alist = split(s:str)
endfunction

" }])>

" GS_def <([{
function! s:GS_def()
call s:initInput()

" Call gs
let s:str = system('./gs def ' . s:file_name . ' ' . s:symbol)
if s:str == ''
	let s:str = system('./gs def -- ' . s:symbol)
	if s:str == ''
		return
	endif
endif

call s:showSelection()

call s:jumpTo()
endfunction
" }])>

" GS_called <([{
function! s:GS_called()
call s:initInput()

" Call gs
let s:str = system('./gs callee -- ' . s:symbol)
if s:str == ''
	return
endif

call s:showSelection()

call s:jumpTo()
endfunction
" }])>

" Note:
" To walk through file by file-offset, try `:go file-offset'.
" To see where char-offset is from file, try `g<CTRL-g>' on the char.

nmap <C-]> :call <SID>GS_def()<CR>
nmap <C-[> :call <SID>GS_called()<CR>
