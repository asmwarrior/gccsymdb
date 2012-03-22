-- vim: foldmarker=<([{,}])> foldmethod=marker
create table ProjectOverview (
	dbVersion text,
	gccVersion text,
	projectRootPath text,
	-- plugin control parameters.
	debug boolean
);

-- Useful sql clauses set <([{
-- create table Directive (
-- 	id integer primary key autoincrement,
-- 	fileOffset integer
-- );

-- }])>

-- File <([{
/**
 * Here, the field mtime stores C library stat.st_mtime.
 */
create table chFile (
	id integer primary key autoincrement,
	fullName text, -- absolute path.
	mtime bigint,
	sysHeader boolean
);

create table iFile (
	id integer primary key autoincrement,
	mainFileID integer, -- chFile::id.
	hasError integer, -- 0: no error; 1: c preprocess stage error; 2: c/c++ error.
	iTokenStartID bigint,
	iTokenEndID bigint,
	syntaxTreeStartID bigint,
	syntaxTreeEndID bigint
);

create table FileDependence (
	id integer primary key autoincrement,
	iFileID integer,
	hID integer
);

create view CompileUnit as
select
	b.fullName as cFile, b.mtime as cTime, c.fullName as hFile, c.mtime as hTime
from
	iFile as a, chFile as b, chFile as c, FileDependence as d
where
	a.mainFileID = b.id and a.id = d.iFileID and c.id = d.hID;

create index FileName on chFile (fullName);
-- }])>

-- Token <([{
create table chToken (
	id integer primary key autoincrement,
	flag integer, -- see symdb document
	length integer,
	chFileID integer,
	fileOffset integer
);

create table MacroDescription (
	id integer primary key autoincrement,
	leaderchTokenID bigint,
	expandedCount integer,
	macroCount integer,
	macroStartID bigint
);

create table MacroToken (
	id integer primary key autoincrement,
	flag, -- see symdb document
	value text
);

create table iToken (
	id integer primary key autoincrement,
	flag integer, -- see symdb document
	length integer,
	value text,
	relattedTokenID bigint -- chToken::id or MacroToken::id.
);

create view FileToken as
select * from
(
select
	f.id as fileID, fileOffset, t.id as tokenID, flag, length
from
	chFile f, chToken as t
where
	f.id = t.chFileID
order by
	fileID, fileOffset
) as x
left outer join
(
select
	a.id as tokenID, b.id as macroDescriptionID
from
	chToken as a, MacroDescription as b
where
	a.id = b.leaderchTokenID
) as y
on
	x.tokenID = y.tokenID;

create index indexTokenFromFile on chToken (chFileID, fileOffset asc);

create index indexMacroWeld on MacroDescription (leaderchTokenID);

create trigger deleteTokenFromFile delete on chToken
begin
	delete from MacroDescription where leaderchTokenID = old.id;
end;

create trigger deleteTokenFromFile2 delete on MacroDescription
begin
	delete from MacroToken where id >= old.macroStartID and id < old.macroStartID + old.macroCount;
end;
-- }])>

-- Tree <([{
/**
 * @code the field are divided into three parts.
 * 		1) the highest 8 bits, only one bits are defined currently, which are called global bits.
 * 		2) the lowest 16 bits are tree code. gcc/tree.h:enum tree_code.
 * 	the middle 8 bits:
 * 		1) type decoration code (scp, ts, tq) = MAX_TREE_CODES * 2.
 * 		2) array header code = 			MAX_TREE_CODES * 2 + 1.
 * 		3) pointer header code = 		MAX_TREE_CODES * 2 + 2.
 * 		4) attribute header code = 		MAX_TREE_CODES * 2 + 3.
 * 		5) initializer header code = 	MAX_TREE_CODES * 2 + 4.
 */
create table SyntaxTree (
	id integer primary key autoincrement,
	code integer,
	-- To compound-statement tree, store start/end treeID.
	-- To VAR_DECL, FUNCTION_DECL etc. value = SemanticTree::id.
	value blob,
	parentTreeID integer -- store relative treeID of compound-statement head.
);

-- Initializer; function; struct;
create table CompountStatment (
	id integer primary key autoincrement,
	code integer,
	value integer,
	value2 integer,
	parentTreeID integer
);

create table SemanticTree (
	id integer primary key autoincrement,
	syntaxTreeID integer,
	scopeCPPTokenID integer,
	declCPPTokenID integer,
	tokenID integer -- chToken::id or MacroToken::id.
);

-- temporary table.
create table TemporaryTree (
	id integer primary key autoincrement,
	memAddr integer,
	code integer,
	value blob,
	parentTreeID integer
);

-- create view global, iFile::local tree.
-- If two trees are equal, the SemanticTree of their roots (user-defined identifier) are equal.
-- create view __ as identifier, chFileID, cppTokenID, treeCode, scopeCPPTokenID, declCPPTokenID.
-- create view ExternalDeclaration.
-- compare tree code and identifier to global declaration.
-- store statement tree scope to file.
-- how to link tree back to cpp token. A temporary ifile--cpptoken table.
-- }])>

-- initialization sample <([{
insert into ProjectOverview values ("1.0", "4.6.2", "/home/zyf/src/symdb.gcc", "true");
insert into iToken (id) values (NULL); -- to make inner table `sqlite_sequence' including a record for iToken.
-- }])>

-- NULL <([{
-- }])>
