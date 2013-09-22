-- vim: foldmarker=<([{,}])> foldmethod=marker
create table ProjectOverview (
	dbVersion text,
	pluginVersion text,
	gccVersion text,
	sqliteVersion text,
	projectRootPath text,
	initdbTime bigint, -- unit is second.
	enddbTime bigint,
	-- User-defined info, such as the svn revision of code.
	userDefInfo text,
	-- plugin control parameters.
	canUpdateFile boolean
);
insert into ProjectOverview values ("1.0", "X.0", "4.6.X", "3.6.X", "/project/root/path/", 0, 0, "user data", 't');

-- chFile is the root of all tables, see trigger fold for more, delete the table will delete all things in the file.
-- But currently, macro feature doesn't depend on chFile table, which is transient.

-- File tables <([{
create table chFile (
	id integer primary key autoincrement,
	name text,
	mtime bigint,
	sysHeader boolean
);

-- a c/h file and its direct-including .h.
create table FileDependence (
	chID integer references chFile (id),
	hID integer references chFile (id),
	offset integer -- `#include' token file-offset.
);
-- }])>

-- Definition tables <([{
create table Definition (
	id integer primary key autoincrement,
	fileID integer references chFile (id),
	name text,
	flag integer,
	fileoffset integer
);

create table FunctionRelationship (
	caller bigint,
	callee bigint
);
-- }])>

-- The table stores the information of which lines are skipped by such like `ifdef/if'.
create table Ifdef (
	fileID integer references chFile (id),
	flag integer,
	startOffset integer,
	endOffset integer
);

-- For function alias feature.
-- The table is used to store where a member function pointer is assigned.
create table FunpAlias (
	fileID integer references chFile (id),
	structName text,
	member text,
	funDecl text,
	offset integer
);

-- For macro feature of `./gs macro XX'.
create table Macro (
	-- user request.
	letFileID integer,
	letOffset integer, 

	-- result.
	defFileID integer,
	defFileOffset integer,
	expandedTokens text,
	macroTokens text
);

-- For member offset feature.
-- If member field is '', offset represents the size of the struct.
create table Offsetof (
	structID integer references Definition (id),
	member text,
	offset integer
);

-- Useful views, search them instead of original tables if possible <([{
-- Search file-definition pair.
create view FileSymbol as
select * from
(
select
	f.id as fileID, f.name as fileName, fileoffset, d.id as defID, d.name as defName, flag
from
	chFile f, Definition as d
where
	d.fileID = f.id
);

-- Search function-call relationship.
create view CallRelationship as
select * from
(
select
	f.id as fileID, f.name as fileName,
	d1.fileOffset as callerFileOffset, caller as callerID, d1.name as callerName,
	d2.fileOffset as calleeFileOffset, callee as calleeID, d2.name as calleeName,
	d2.flag
from
	FunctionRelationship dr, chFile f, Definition as d1, Definition as d2
where
	dr.caller = d1.id and dr.callee = d2.id and
	d1.fileID = f.id
);
-- }])>

-- Index <([{
create index FileName on chFile (name);

create index DefName on Definition (name);

create index Alias on FunpAlias (member, funDecl); 
-- }])>

-- Triggger <([{
-- Delete trigger note: all can be indexed directely or indirectely by chFile::id. But to Definition fold, two additional trigger must be set up.
create trigger DelDefinition after delete on Definition
begin
	delete from FunctionRelationship where caller = old.id;
	delete from Offsetof where structID = old.id;
end;

create trigger DelFile after delete on chFile
begin
	delete from FileDependence where chID = old.id;
	delete from Definition where fileID = old.id;
	delete from Ifdef where fileID = old.id;
	delete from FunpAlias where fileID = old.id;
end;
-- }])>
