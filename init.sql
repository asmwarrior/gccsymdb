-- vim: foldmarker=<([{,}])> foldmethod=marker
-- Synchronize helper.c:initdb() if the table is changed.
create table ProjectOverview (
	dbVersion text,
	pluginVersion text,
	gccVersion text,
	projectRootPath text
	-- plugin control parameters.
);

-- File tables <([{
create table chFile (
	id integer primary key autoincrement,
	name text,
	mtime bigint,
	sysHeader boolean
);

-- a c/h file and its direct-including .h.
create table FileDependence (
	chFileID integer,
	hID integer
);
-- }])>

-- Definition tables <([{
create table Definition (
	id integer primary key autoincrement,
	name text,
	flag integer,
	position integer
);

-- Not only function-definition and its callee functions, in the future, the table can be used as other similar relationship.
create table DefinitionRelationship (
	caller bigint,
	callee bigint
);
-- }])>

create table FileDefinition (
	fileID integer,
	startDefID bigint,
	endDefID bigint
);

-- Useful views <([{
create view Helper as
select * from
(
select
	f.id as fileID, f.name as fileName, position, d.id as defID, d.name as defName, flag
from
	chFile f, Definition as d, FileDefinition fd
where
	fd.fileID = f.id and
	fd.startDefID <= d.id and fd.endDefID >= d.id
);
-- }])>

-- Index <([{
create index FileName on chFile (name);

create index DefName on Definition (name);
-- }])>

insert into ProjectOverview values ("1.0", "2.0", "4.6.2", "/project/root/path/");
