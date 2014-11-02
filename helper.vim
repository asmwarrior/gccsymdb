" vim: foldmarker=<([{,}])> foldmethod=marker

" Common <([{
" Temporary varaibles are used anywhere.
let s:str = ''
let s:i = 0
let s:j = 0
let s:alist = []

" Get filename and identifier.
function! s:noInput()
	let s:file_name = fnamemodify(bufname('%'), '')
	let s:symbol = expand('<cword>')
endfunction

function! s:go_offset(fname, offset)
	let s:i = bufwinnr(a:fname)
	if s:i == -1
		execute 'ed +go\ ' . a:offset . ' ' . a:fname
	else
		execute s:i . 'wincmd w'
		execute 'go ' . a:offset
	endif
endfunction

let s:jump_history = []
" Jump to the tag.
function! s:jumpTo()
	let s:file_name = get(s:alist, 0)
	let s:file_offset = get(s:alist, 1) + 1
	call add(s:jump_history, fnamemodify(bufname('%'), ''))
	call add(s:jump_history, col('.') + line2byte(line('.')) - 1)
	call s:go_offset(s:file_name, s:file_offset)
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
	call s:go_offset(s:file_name, s:file_offset)
endfunction

" Show multiple selection and get user-input.
function! s:showSelection()
	let s:alist = split(s:str, '\n')
	let s:selection = len(s:alist)
	if s:selection != 1
		echo 'GS multiple definition:'
		let s:i = 1
		for s:element in s:alist
			echo s:i . ') ' . s:element
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

	" let s:str = system('./gs def ' . s:file_name . ' ' . s:symbol)
	let s:str = system('./gs def -- ' . s:symbol)
	if s:str == ''
		echo "Not found."
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
	let s:str = system('./gs callee ' . s:symbol)
	if s:str == ''
		echo "Not found."
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
function! s:GS_falias()
	let s:str = system('./gs falias ' . s:symbol)
	if s:str == ''
		echo "Not found."
		return
	endif

	let s:i = s:showSelection()
	if s:i == -1
		return
	endif
	call s:jumpTo()
endfunction
" }])>
" Gs_list <([{
" Code is mainly coming from taglist.vim.
let gs_taglist_window_title = "__Gs_TagList__"
let s:taglist = {}
let s:taglist_file = ''

function! s:GS_Window_Jump_To_Tag()
	let s:i = line('.')
	if has_key(s:taglist, s:i)
		let s:file_offset = get(s:taglist[s:i], 1) + 1
		let s:i = bufwinnr(s:taglist_file)
		call s:go_offset(s:taglist_file, s:file_offset)
	endif
endfunction

function! s:GS_Load_Tag(flag, line)
	let s:str = './gs '
	if a:flag == 1
		let s:str = s:str . 'listtsue '
	elseif a:flag == 2
		let s:str = s:str . 'listvar '
	elseif a:flag == 3
		let s:str = s:str . 'listfunc '
	elseif a:flag == 4
		let s:str = s:str . 'listenumerator '
	elseif a:flag == 5
		let s:str = s:str . 'listmacro '
	endif
	let s:str = s:str . s:taglist_file
	let s:alist = split(system(s:str), '\n')
	let s:i = 0
	for s:element in s:alist
		let l:tmp = split(s:element)
		call append(a:line + s:i, '  ' . get(l:tmp, 0))
		let s:taglist[a:line + s:i + 1] = l:tmp
		let s:i += 1
	endfor
	return len(s:alist)
endfunction

function! s:GS_Window_Refresh()
	silent! setlocal modifiable
	silent! setlocal noreadonly
	silent! %delete _
	let s:str = system('./gs testfile ' . s:taglist_file)
	if v:shell_error != 0
		call append(1, s:taglist_file . ' ' . get(split(s:str, '\n'), 0))
	else
		call append(1, s:taglist_file)
		let l:current = 2
		call append(l:current, "struct/enum/union/typedef")
		let l:current += 1
		let l:count = s:GS_Load_Tag(1, l:current)
		execute l:current . ',' . (l:current + l:count) . 'fold'
		let l:current += l:count
		call append(l:current, "var")
		let l:current += 1
		let l:count = s:GS_Load_Tag(2, l:current)
		execute l:current . ',' . (l:current + l:count) . 'fold'
		let l:current += l:count
		call append(l:current, "function")
		let l:current += 1
		let l:count = s:GS_Load_Tag(3, l:current)
		execute l:current . ',' . (l:current + l:count) . 'fold'
		execute l:current . ',' . (l:current + l:count) . 'foldopen'
		let l:current += l:count
		call append(l:current, "enumerator")
		let l:current += 1
		let l:count = s:GS_Load_Tag(4, l:current)
		execute l:current . ',' . (l:current + l:count) . 'fold'
		let l:current += l:count
		call append(l:current, "macro")
		let l:current += 1
		let l:count = s:GS_Load_Tag(5, l:current)
		execute l:current . ',' . (l:current + l:count) . 'fold'
	endif
	silent! setlocal nomodifiable
	silent! setlocal nomodified
endfunction

function! s:GS_Window_Create()
	execute 'silent! topleft vertical 30 vsplit ' . g:gs_taglist_window_title

	setlocal foldenable
	setlocal foldminlines=0
	setlocal foldmethod=manual
	setlocal foldlevel=9999
	setlocal foldcolumn=1
	setlocal foldtext=v:folddashes.getline(v:foldstart)

	syntax match TagListTitle '^\S.*$'
	highlight clear TagListTitle
	highlight default link TagListTitle Title

	silent! setlocal nowrap
	silent! setlocal nonumber
	silent! setlocal nobuflisted
	silent! setlocal buftype=nofile
	silent! setlocal noswapfile
	silent! setlocal bufhidden=delete

	nnoremap <buffer> <silent> <CR> :call <SID>GS_Window_Jump_To_Tag()<CR>
endfunction

function! s:GS_list()
	" Close the taglist window.
	let s:i = bufwinnr(g:gs_taglist_window_title)
	let s:j = winnr()
	if s:i != -1
		if s:i == s:j
			close
		else
			execute s:i . 'wincmd w'
			close
			execute s:j . 'wincmd w'
		endif
	endif

	let s:taglist_file = fnamemodify(bufname('%'), '')
	call s:GS_Window_Create()
	call s:GS_Window_Refresh()
endfunction
" }])>

" GS_cmd, `:Gs help' about how to use the script <([{
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
	elseif a:subcmd == 'list'
		call s:GS_list()
	else
		echo 'Gs command (all except ifdef accept only a param):'
		echo 'def     : search a definition.'
		echo 'callee  : where a function is called, calling mfp is also searched cascadedly'
		echo 'ifdef   : whether current position is skipped by ifdef/if.'
		echo 'falias  : search where a mfp jumps to.'
		echo 'list    : open a taglist window.'
	endif
endfunction
" }])>

" Note:
" To walk through file by file-offset, try `:go file-offset'.
" To see where char-offset is from file, try `g<CTRL-g>' on the char.

command! -nargs=* Gs  call <SID>GS_cmd(<f-args>)
" Some key-shortcuts overload vim symbol-tag commands.
nmap <C-]> :call <SID>GS_def(0)<CR>
nmap <C-[> :call <SID>GS_callee(0)<CR>
nmap <C-T> :call <SID>GS_jumpback()<CR>
