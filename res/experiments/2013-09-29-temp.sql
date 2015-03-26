--
-- PostgreSQL database dump
--

SET statement_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SET check_function_bodies = false;
SET client_min_messages = warning;

--
-- Name: plpgsql; Type: EXTENSION; Schema: -; Owner: 
--

CREATE EXTENSION IF NOT EXISTS plpgsql WITH SCHEMA pg_catalog;


--
-- Name: EXTENSION plpgsql; Type: COMMENT; Schema: -; Owner: 
--

COMMENT ON EXTENSION plpgsql IS 'PL/pgSQL procedural language';


SET search_path = public, pg_catalog;

SET default_tablespace = '';

SET default_with_oids = false;

--
-- Name: fileHashes; Type: TABLE; Schema: public; Owner: postgres; Tablespace: 
--

CREATE TABLE "fileHashes" (
    "fileHashID" bigint NOT NULL,
    "fileID" bigint NOT NULL,
    "hashID" bigint NOT NULL
);


ALTER TABLE public."fileHashes" OWNER TO postgres;

--
-- Name: fileHashes_fileHashID_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE "fileHashes_fileHashID_seq"
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public."fileHashes_fileHashID_seq" OWNER TO postgres;

--
-- Name: fileHashes_fileHashID_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE "fileHashes_fileHashID_seq" OWNED BY "fileHashes"."fileHashID";


--
-- Name: fileIndexes; Type: TABLE; Schema: public; Owner: postgres; Tablespace: 
--

CREATE TABLE "fileIndexes" (
    "fileIndexID" bigint NOT NULL,
    "sourceFileID" bigint NOT NULL,
    "targetFileID" bigint NOT NULL,
    index tsvector NOT NULL
);


ALTER TABLE public."fileIndexes" OWNER TO postgres;

--
-- Name: fileIndexes_fileIndexID_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE "fileIndexes_fileIndexID_seq"
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public."fileIndexes_fileIndexID_seq" OWNER TO postgres;

--
-- Name: fileIndexes_fileIndexID_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE "fileIndexes_fileIndexID_seq" OWNED BY "fileIndexes"."fileIndexID";


--
-- Name: fileLinks; Type: TABLE; Schema: public; Owner: postgres; Tablespace: 
--

CREATE TABLE "fileLinks" (
    "fileLinkID" bigint NOT NULL,
    "fileID" bigint NOT NULL,
    "normalizedURI" text NOT NULL
);


ALTER TABLE public."fileLinks" OWNER TO postgres;

--
-- Name: fileLinks_fileLinkID_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE "fileLinks_fileLinkID_seq"
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public."fileLinks_fileLinkID_seq" OWNER TO postgres;

--
-- Name: fileLinks_fileLinkID_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE "fileLinks_fileLinkID_seq" OWNED BY "fileLinks"."fileLinkID";


--
-- Name: files; Type: TABLE; Schema: public; Owner: postgres; Tablespace: 
--

CREATE TABLE files (
    "fileID" bigint NOT NULL,
    "internalHash" text NOT NULL,
    type text NOT NULL,
    size bigint NOT NULL
);


ALTER TABLE public.files OWNER TO postgres;

--
-- Name: files_fileID_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE "files_fileID_seq"
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public."files_fileID_seq" OWNER TO postgres;

--
-- Name: files_fileID_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE "files_fileID_seq" OWNED BY files."fileID";


--
-- Name: hashes; Type: TABLE; Schema: public; Owner: postgres; Tablespace: 
--

CREATE TABLE hashes (
    "hashID" bigint NOT NULL,
    algorithm text NOT NULL,
    hash text NOT NULL
);


ALTER TABLE public.hashes OWNER TO postgres;

--
-- Name: hashes_hashID_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE "hashes_hashID_seq"
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public."hashes_hashID_seq" OWNER TO postgres;

--
-- Name: hashes_hashID_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE "hashes_hashID_seq" OWNED BY hashes."hashID";


--
-- Name: pulls; Type: TABLE; Schema: public; Owner: postgres; Tablespace: 
--

CREATE TABLE pulls (
    "pullID" bigint NOT NULL,
    "userID" bigint NOT NULL,
    targets text NOT NULL,
    "URI" text NOT NULL,
    "queryString" text NOT NULL,
    "queryLanguage" text NOT NULL,
    username text NOT NULL,
    password text NOT NULL
);


ALTER TABLE public.pulls OWNER TO postgres;

--
-- Name: pulls_pullID_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE "pulls_pullID_seq"
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public."pulls_pullID_seq" OWNER TO postgres;

--
-- Name: pulls_pullID_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE "pulls_pullID_seq" OWNED BY pulls."pullID";


--
-- Name: sessions; Type: TABLE; Schema: public; Owner: postgres; Tablespace: 
--

CREATE TABLE sessions (
    "sessionID" bigint NOT NULL,
    "sessionHash" text NOT NULL,
    "userID" bigint NOT NULL,
    "modeRead" boolean NOT NULL,
    "modeWrite" boolean NOT NULL,
    "timestamp" timestamp without time zone DEFAULT now() NOT NULL
);


ALTER TABLE public.sessions OWNER TO postgres;

--
-- Name: sessions_sessionID_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE "sessions_sessionID_seq"
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public."sessions_sessionID_seq" OWNER TO postgres;

--
-- Name: sessions_sessionID_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE "sessions_sessionID_seq" OWNED BY sessions."sessionID";


--
-- Name: submissions; Type: TABLE; Schema: public; Owner: postgres; Tablespace: 
--

CREATE TABLE submissions (
    "submissionID" bigint NOT NULL,
    "fileID" bigint NOT NULL,
    "userID" bigint NOT NULL,
    "timestamp" timestamp without time zone DEFAULT now() NOT NULL
);


ALTER TABLE public.submissions OWNER TO postgres;

--
-- Name: sources_sourceID_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE "sources_sourceID_seq"
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public."sources_sourceID_seq" OWNER TO postgres;

--
-- Name: sources_sourceID_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE "sources_sourceID_seq" OWNED BY submissions."submissionID";


--
-- Name: targets; Type: TABLE; Schema: public; Owner: postgres; Tablespace: 
--

CREATE TABLE targets (
    "targetID" bigint NOT NULL,
    "submissionID" bigint NOT NULL,
    "userID" bigint
);


ALTER TABLE public.targets OWNER TO postgres;

--
-- Name: targets_targetID_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE "targets_targetID_seq"
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public."targets_targetID_seq" OWNER TO postgres;

--
-- Name: targets_targetID_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE "targets_targetID_seq" OWNED BY targets."targetID";


--
-- Name: users; Type: TABLE; Schema: public; Owner: postgres; Tablespace: 
--

CREATE TABLE users (
    "userID" bigint NOT NULL,
    username text NOT NULL,
    "passwordHash" text NOT NULL,
    "tokenHash" text NOT NULL,
    cert text,
    key text,
    "timestamp" timestamp without time zone DEFAULT now() NOT NULL,
    CONSTRAINT "usernameNotPublic" CHECK ((username <> 'public'::text))
);


ALTER TABLE public.users OWNER TO postgres;

--
-- Name: users_userID_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE "users_userID_seq"
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE public."users_userID_seq" OWNER TO postgres;

--
-- Name: users_userID_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE "users_userID_seq" OWNED BY users."userID";


--
-- Name: fileHashID; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY "fileHashes" ALTER COLUMN "fileHashID" SET DEFAULT nextval('"fileHashes_fileHashID_seq"'::regclass);


--
-- Name: fileIndexID; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY "fileIndexes" ALTER COLUMN "fileIndexID" SET DEFAULT nextval('"fileIndexes_fileIndexID_seq"'::regclass);


--
-- Name: fileLinkID; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY "fileLinks" ALTER COLUMN "fileLinkID" SET DEFAULT nextval('"fileLinks_fileLinkID_seq"'::regclass);


--
-- Name: fileID; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY files ALTER COLUMN "fileID" SET DEFAULT nextval('"files_fileID_seq"'::regclass);


--
-- Name: hashID; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY hashes ALTER COLUMN "hashID" SET DEFAULT nextval('"hashes_hashID_seq"'::regclass);


--
-- Name: pullID; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY pulls ALTER COLUMN "pullID" SET DEFAULT nextval('"pulls_pullID_seq"'::regclass);


--
-- Name: sessionID; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY sessions ALTER COLUMN "sessionID" SET DEFAULT nextval('"sessions_sessionID_seq"'::regclass);


--
-- Name: submissionID; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY submissions ALTER COLUMN "submissionID" SET DEFAULT nextval('"sources_sourceID_seq"'::regclass);


--
-- Name: targetID; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY targets ALTER COLUMN "targetID" SET DEFAULT nextval('"targets_targetID_seq"'::regclass);


--
-- Name: userID; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY users ALTER COLUMN "userID" SET DEFAULT nextval('"users_userID_seq"'::regclass);


--
-- Data for Name: fileHashes; Type: TABLE DATA; Schema: public; Owner: postgres
--

COPY "fileHashes" ("fileHashID", "fileID", "hashID") FROM stdin;
7	1	1
8	1	2
9	2	3
10	2	4
11	3	5
12	3	6
13	4	7
14	4	8
\.


--
-- Name: fileHashes_fileHashID_seq; Type: SEQUENCE SET; Schema: public; Owner: postgres
--

SELECT pg_catalog.setval('"fileHashes_fileHashID_seq"', 14, true);


--
-- Data for Name: fileIndexes; Type: TABLE DATA; Schema: public; Owner: postgres
--

COPY "fileIndexes" ("fileIndexID", "sourceFileID", index, "targetFileID") FROM stdin;
\.


--
-- Name: fileIndexes_fileIndexID_seq; Type: SEQUENCE SET; Schema: public; Owner: postgres
--

SELECT pg_catalog.setval('"fileIndexes_fileIndexID_seq"', 1, false);


--
-- Data for Name: fileLinks; Type: TABLE DATA; Schema: public; Owner: postgres
--

COPY "fileLinks" ("fileLinkID", "fileID", "normalizedURI") FROM stdin;
\.


--
-- Name: fileLinks_fileLinkID_seq; Type: SEQUENCE SET; Schema: public; Owner: postgres
--

SELECT pg_catalog.setval('"fileLinks_fileLinkID_seq"', 1, false);


--
-- Data for Name: files; Type: TABLE DATA; Schema: public; Owner: postgres
--

COPY files ("fileID", "internalHash", type, size) FROM stdin;
1	21cb5b91a68342b696688edd66318220c0a4eec0	text/javascript	1892
2	da915b2403f287192cdca474412553285ebc99e4	text/markdown	8
3	f4811e714b0f63dc6fc16734a68c838614292a09	text/plain; charset=utf-8	2035
4	51b2748050f296a6f7bad71c1b4698a7a9c5fd9c	text/plain; charset=utf-8	1292
\.


--
-- Name: files_fileID_seq; Type: SEQUENCE SET; Schema: public; Owner: postgres
--

SELECT pg_catalog.setval('"files_fileID_seq"', 4, true);


--
-- Data for Name: hashes; Type: TABLE DATA; Schema: public; Owner: postgres
--

COPY hashes ("hashID", algorithm, hash) FROM stdin;
1	sha1	21cb5b91a68342b696688edd66318220c0a4eec0
2	sha1	IctbkaaDQraWaI7dZjGCIMCk7sA=
3	sha1	da915b2403f287192cdca474412553285ebc99e4
4	sha1	2pFbJAPyhxks3KR0QSVTKF68meQ=
5	sha1	f4811e714b0f63dc6fc16734a68c838614292a09
6	sha1	9IEecUsPY9xvwWc0poyDhhQpKgk=
7	sha1	51b2748050f296a6f7bad71c1b4698a7a9c5fd9c
8	sha1	UbJ0gFDylqb3utccG0aYp6nF/Zw=
\.


--
-- Name: hashes_hashID_seq; Type: SEQUENCE SET; Schema: public; Owner: postgres
--

SELECT pg_catalog.setval('"hashes_hashID_seq"', 8, true);


--
-- Data for Name: pulls; Type: TABLE DATA; Schema: public; Owner: postgres
--

COPY pulls ("pullID", "userID", targets, "URI", "queryString", "queryLanguage", username, password) FROM stdin;
\.


--
-- Name: pulls_pullID_seq; Type: SEQUENCE SET; Schema: public; Owner: postgres
--

SELECT pg_catalog.setval('"pulls_pullID_seq"', 1, false);


--
-- Data for Name: sessions; Type: TABLE DATA; Schema: public; Owner: postgres
--

COPY sessions ("sessionID", "sessionHash", "userID", "modeRead", "modeWrite", "timestamp") FROM stdin;
1	$2a$10$CYdvhtPPe8fIi60LI1Bw3O.Jr4leFBoBgZwqB66ENPcDhip5G96.u	1	t	t	2013-09-20 10:48:38.448414
2	$2a$10$1n7blNgAjcJOZtcA0zmRMeFVFmYXACal.vXW3qxmUwBNUC2sU32Cq	1	t	t	2013-09-20 10:54:33.216079
3	$2a$10$cxqQ2./N9EiTSeUyViSMvuAHkbRIWreWARb7Lqdquz2nIwlxQ7TZq	1	t	t	2013-09-20 10:55:17.507057
4	$2a$10$dvKEK08UIqKIomiQj.BoUucZzLbgEtr3V6FnH/OqqRZEqydO.ZrCG	1	t	t	2013-09-20 10:56:23.421298
5	$2a$10$GaP0kpLQ9MYJ1L247WweM..eYVG42ZREwRrSe/UFeQocfi3uy.e9e	1	t	t	2013-09-20 10:57:48.031823
6	$2a$10$FFoIiGGbHVsrhtVp8THP1OS/j6otI5nrbJ5NC4q1Izp.ZCNidI0zy	1	t	t	2013-09-20 10:58:10.873057
7	$2a$10$xO9fpdvK5NNKVBuVE88iFO/MO/QfxPgGg3vitmDOthQObNVWimHvq	1	t	t	2013-09-20 10:58:15.175581
8	$2a$10$TL3oRENQPkR3P7X5HhsrPeAPirFGIqxrnUWhgDWsgunc6vLDJJZPG	1	t	t	2013-09-20 10:59:18.275896
9	$2a$10$oAbP/qByy4ZN/0uLgoouGurYXLUN5XqSOqlqua9iJ4Mt9F1dYUXHm	1	t	t	2013-09-20 10:59:33.598898
10	$2a$10$cWbY2jdJ6j6pOwufkFRdZuqWXlApnZjuP2vLEqPWaHvwnu4e2dYJq	1	t	t	2013-09-20 10:59:40.312311
11	$2a$10$KNZ.QS5ufc/tCSaes9PdCeLIfL/pBjtsLimjFRR88Cq4NGhWqxVKO	1	t	t	2013-09-20 11:00:54.167437
12	$2a$10$NW0D8rcTv4vwYVprJz/NJuca.9XgnO8RbueopTJQEYkgXSCbPlKbS	1	t	t	2013-09-20 11:02:07.89562
13	$2a$10$TFa0zUzxF20vhzXNsVFXU.6OfVwFSWYZ8V/.iw9zcDl3T51rBADHe	1	t	t	2013-09-20 11:02:53.136541
14	$2a$10$giH3RHjWS1SgZK85yUIddus.Y5NRMVbuivq4Pw8FSQSiP9Bd1IQoG	1	t	t	2013-09-20 11:10:03.513176
15	$2a$10$hY7/Hhi9CqW0F71CEo1eFuFTW9jkN7FhIdW1eTRlfsu8Sy9D4UVem	1	t	t	2013-09-20 11:10:05.686063
16	$2a$10$feJWpBGtylyKlE.NzAudB.X/cXjDrJbFPKR8YXBmkfDGDUljpFcWC	1	t	t	2013-09-20 11:10:10.600699
17	$2a$10$BSYLtH1vP87z/FHhTIY9F.Y9bApIHE7jCZbq125721wk0VCA4WV7O	1	t	t	2013-09-20 11:34:32.923561
18	$2a$10$CR8W5CRVr1mCbHJEZH4qh.2E6daAD6IvZa9hVXaLBagqZkl83FOpS	1	t	t	2013-09-20 11:38:12.37886
19	$2a$10$WGTlO8j0IRaE3VpvTi0ihe09/RY136Px5E/ZWdHLZrMCLMGi28faS	1	t	t	2013-09-20 11:40:57.76667
20	$2a$10$vG9LeM/OupVd0Wu4oms69uemJO/kfpPzYEWo3P1Z8uWyAghNQ.MEu	1	t	t	2013-09-20 11:42:12.238951
21	$2a$10$P3xWhB.Hv/eR3usHf8kOAeaU2DgJL3zxu9GW/UT3MSm/oXph4mCHS	1	t	t	2013-09-20 11:47:39.127973
22	$2a$10$wIPc8Qpw2vdu/pJ6XO8g0uebuecvNKJycPH0.aGBKz7LGZpuGfYwO	1	t	t	2013-09-20 11:47:55.655879
23	$2a$10$uuCiEmef6R6RLJ0H81MeS.3s8.b6l1FsL6/3dVG4Qm3VEDhmkAKpa	1	t	t	2013-09-29 13:07:49.564262
24	$2a$10$Vs7kO1QB4ddEP2XHqX7FW.yV.fQm8E4Nx0ltQKG2k3NYzyrzTYe9a	1	t	t	2013-09-29 13:08:31.238756
25	$2a$10$RBqZ2q7Ihn84feLUIMm.nO8R4X.fcjiSTlxpFHP2CB9Q2lli9S.Wa	1	t	t	2013-09-29 13:19:37.145209
26	$2a$10$HYAFrhnPwxWokpQAAig8KeVG.AWoYqvM0CZMKcJ1j3D6Dq7T.xF3q	1	t	t	2013-09-29 13:20:25.873849
27	$2a$10$czA5OntUtg6c5HIDkOnPP.1m7mq6PE6Aysk7PPiPAuDqo/mD3U0lu	1	t	t	2013-09-29 13:21:02.730659
28	$2a$10$zyWdpQPIDnU9P4a0Hg6BAOJcrZSPlGwzakLS8AQYM8SOjcARPrgA6	1	t	t	2013-09-29 13:21:53.235175
29	$2a$10$J8Bmemr9t9Qf6EHxgW2aIOx9NtBbony5ChSLA0snyT99FBOgFYNHa	1	t	t	2013-09-29 13:22:52.927387
30	$2a$10$B/kGqzniA36MVLhGBRTWQeOmM3GJggfeo/NjRs1PxBKnM/WI6BA1u	1	t	t	2013-09-29 13:30:44.420816
31	$2a$10$Uqqa2wZy/kWDl2ojpm8RdObJ7GyDl0qj7RcXycPp20VNwShHfjr6K	1	t	t	2013-09-29 13:39:13.779351
32	$2a$10$dAxdfXk5inLawAT/oSRgYeV7YBVzE8OuZNI5NBalw8g/O7rSIaPge	1	t	t	2013-09-29 13:43:07.213645
33	$2a$10$tnuB.Mhb/EBKzT2NDiHKc.Zt1SJ/4DZyxMqJOdCF3i8IophhJd0Cm	1	t	t	2013-09-29 13:43:41.209113
34	$2a$10$n.tK4q08MjigiB.EDcM2luKhSzOh2lUiao4nOMPf6/mMXQk4iXXJG	1	t	t	2013-09-29 13:44:06.065372
35	$2a$10$YmEVi6znHZZrVBCtzII0OeGjHYp6f9k4GuUaHQQ7rim5s7DZnGKsO	1	t	t	2013-09-29 13:44:27.874459
36	$2a$10$a36g84Jbzuwfs/Wb9Eq03uQsrCc9wPhdxZhrnJf4Hiq9tR7LLN.ti	1	t	t	2013-09-29 13:44:36.225831
37	$2a$10$TiwGFu01VfEpUnY4mQLOu.xcA.6oJpV3HtB/tnihQXlmg8mVYAkeG	1	t	t	2013-09-29 13:45:31.318046
38	$2a$10$Db/9N/MTbbworHt.LowY.OiwD0ytp3fX3FjkylKUt3tVkwrdz2ulO	1	t	t	2013-09-29 14:57:52.363425
39	$2a$10$5D5a61FwqWqXjAB/oM.50.dwywk8dKF6XHx5qYmo/4OEiuTRWn7fO	1	t	t	2013-09-29 15:31:41.138271
40	$2a$10$koFSX80sSVRWnJTfIRkCjeSYSdO2TRJ1ADS8sjx/DV5urgQgK146u	1	t	t	2013-09-29 15:34:58.068305
41	$2a$10$cmndwk4S4IIwmxKJX8MNz.oxIBBSm8hACwK1RRE4LEU9TNGYhFLcy	1	t	t	2013-09-29 18:28:10.71268
42	$2a$10$Dbo3rMJluXYKAC6htFnobe.dmLyAsLcjpuvwztznXgoi7sXlaeYq2	1	t	t	2013-09-29 18:28:51.214634
43	$2a$10$z/d4uEL5Ym...d/bvBwG2Op4Brd0bUq.RO74Otbo6aaqCRhSSJiE.	1	t	t	2013-09-29 18:35:57.108425
44	$2a$10$nb6rBEdF9x8IR0KB3QpFfuRkOORO1C4GSkqTbe9Qfa9gYdsnRqI26	1	t	t	2013-09-29 18:36:49.769212
45	$2a$10$ND1Oe1D1Dcxz6XNSMddClODBSQSSumZ9RnPWrXgEg3OLljWXyAr6a	1	t	t	2013-09-29 18:37:14.042344
46	$2a$10$YtqreTQtkvzOIjgEY4YWqefPkm45EMPSlu5HPnUhnkvzhpbFtHvIy	1	t	t	2013-09-29 18:38:51.24057
47	$2a$10$I5rZ8wZ33B9sFRx759sRhu6y6tWsk0klbg3MeMmhmfuJNJ1jCtqFe	1	t	t	2013-09-29 18:39:56.034831
48	$2a$10$OcRAlgKBJyRgNW728Ie24.G5YRZUX.3FsxTUn9Y6GrxtjdWKv6ZfO	1	t	t	2013-09-29 18:40:32.17078
49	$2a$10$nAGaz9GEMv6.680kxKEagu8XrI3oxDtydAQzlHbWX2mTG7dpnfUzK	1	t	t	2013-09-29 19:15:43.469119
\.


--
-- Name: sessions_sessionID_seq; Type: SEQUENCE SET; Schema: public; Owner: postgres
--

SELECT pg_catalog.setval('"sessions_sessionID_seq"', 49, true);


--
-- Name: sources_sourceID_seq; Type: SEQUENCE SET; Schema: public; Owner: postgres
--

SELECT pg_catalog.setval('"sources_sourceID_seq"', 10, true);


--
-- Data for Name: submissions; Type: TABLE DATA; Schema: public; Owner: postgres
--

COPY submissions ("submissionID", "fileID", "userID", "timestamp") FROM stdin;
1	1	1	2013-09-20 10:48:38.515669
2	1	1	2013-09-20 10:58:11.015325
3	1	1	2013-09-20 10:59:33.658379
4	1	1	2013-09-20 11:10:03.571445
5	1	1	2013-09-20 11:10:05.702593
6	2	1	2013-09-20 11:47:39.188591
7	3	1	2013-09-29 13:07:49.622865
8	3	1	2013-09-29 13:19:37.156271
9	4	1	2013-09-29 18:39:56.067154
10	4	1	2013-09-29 18:40:32.180826
\.


--
-- Data for Name: targets; Type: TABLE DATA; Schema: public; Owner: postgres
--

COPY targets ("targetID", "submissionID", "userID") FROM stdin;
1	1	1
2	2	1
3	3	1
4	4	1
5	5	1
6	6	1
7	7	1
8	8	1
9	9	1
10	10	1
\.


--
-- Name: targets_targetID_seq; Type: SEQUENCE SET; Schema: public; Owner: postgres
--

SELECT pg_catalog.setval('"targets_targetID_seq"', 10, true);


--
-- Data for Name: users; Type: TABLE DATA; Schema: public; Owner: postgres
--

COPY users ("userID", username, "passwordHash", "tokenHash", cert, key, "timestamp") FROM stdin;
1	ben	$2a$10$hY5JyMhIVGiwNsvTwYHhUe.QFjPaULz/adHE50KzQ53.bX0Qu/TaW	$2a$10$hY5JyMhIVGiwNsvTwYHhUe.QFjPaULz/adHE50KzQ53.bX0Qu/TaW	\N	\N	2013-09-20 10:47:58.146989
\.


--
-- Name: users_userID_seq; Type: SEQUENCE SET; Schema: public; Owner: postgres
--

SELECT pg_catalog.setval('"users_userID_seq"', 1, true);


--
-- Name: fileHashesPrimaryKey; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY "fileHashes"
    ADD CONSTRAINT "fileHashesPrimaryKey" PRIMARY KEY ("fileHashID");


--
-- Name: fileIndexesPrimaryKey; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY "fileIndexes"
    ADD CONSTRAINT "fileIndexesPrimaryKey" PRIMARY KEY ("fileIndexID");


--
-- Name: fileLinksPrimaryKey; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY "fileLinks"
    ADD CONSTRAINT "fileLinksPrimaryKey" PRIMARY KEY ("fileLinkID");


--
-- Name: fileLinksUnique; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY "fileLinks"
    ADD CONSTRAINT "fileLinksUnique" UNIQUE ("fileID", "normalizedURI");


--
-- Name: filePrimaryKey; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY files
    ADD CONSTRAINT "filePrimaryKey" PRIMARY KEY ("fileID");


--
-- Name: filesUniqueHashAndType; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY files
    ADD CONSTRAINT "filesUniqueHashAndType" UNIQUE ("internalHash", type);


--
-- Name: hashesPrimaryKey; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY hashes
    ADD CONSTRAINT "hashesPrimaryKey" PRIMARY KEY ("hashID");


--
-- Name: hashesUnique; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY hashes
    ADD CONSTRAINT "hashesUnique" UNIQUE (algorithm, hash);


--
-- Name: pullsPrimaryKey; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY pulls
    ADD CONSTRAINT "pullsPrimaryKey" PRIMARY KEY ("pullID");


--
-- Name: sessions_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY sessions
    ADD CONSTRAINT sessions_pkey PRIMARY KEY ("sessionID");


--
-- Name: submissionsPrimaryKey; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY submissions
    ADD CONSTRAINT "submissionsPrimaryKey" PRIMARY KEY ("submissionID");


--
-- Name: targets_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY targets
    ADD CONSTRAINT targets_pkey PRIMARY KEY ("targetID");


--
-- Name: usersUniqueName; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY users
    ADD CONSTRAINT "usersUniqueName" UNIQUE (username);


--
-- Name: users_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres; Tablespace: 
--

ALTER TABLE ONLY users
    ADD CONSTRAINT users_pkey PRIMARY KEY ("userID");


--
-- Name: hashesIndex; Type: INDEX; Schema: public; Owner: postgres; Tablespace: 
--

CREATE INDEX "hashesIndex" ON hashes USING btree (algorithm, hash);


--
-- Name: fileHashesOnDuplicateDoNothing; Type: RULE; Schema: public; Owner: postgres
--

CREATE RULE "fileHashesOnDuplicateDoNothing" AS ON INSERT TO "fileHashes" WHERE (EXISTS (SELECT 1 FROM "fileHashes" old WHERE ((old."fileID" = new."fileID") AND (old."hashID" = new."hashID")))) DO INSTEAD NOTHING;


--
-- Name: fileLinksOnDuplicateDoNothing; Type: RULE; Schema: public; Owner: postgres
--

CREATE RULE "fileLinksOnDuplicateDoNothing" AS ON INSERT TO "fileLinks" WHERE (EXISTS (SELECT 1 FROM "fileLinks" old WHERE ((old."fileID" = new."fileID") AND (old."normalizedURI" = new."normalizedURI")))) DO INSTEAD NOTHING;


--
-- Name: filesOnDuplicateDoNothing; Type: RULE; Schema: public; Owner: postgres
--

CREATE RULE "filesOnDuplicateDoNothing" AS ON INSERT TO files WHERE (EXISTS (SELECT 1 FROM files old WHERE ((old."internalHash" = new."internalHash") AND (old.type = new.type)))) DO INSTEAD NOTHING;


--
-- Name: hashesOnDuplicateDoNothing; Type: RULE; Schema: public; Owner: postgres
--

CREATE RULE "hashesOnDuplicateDoNothing" AS ON INSERT TO hashes WHERE (EXISTS (SELECT 1 FROM hashes old WHERE ((old.algorithm = new.algorithm) AND (old.hash = new.hash)))) DO INSTEAD NOTHING;


--
-- Name: public; Type: ACL; Schema: -; Owner: postgres
--

REVOKE ALL ON SCHEMA public FROM PUBLIC;
REVOKE ALL ON SCHEMA public FROM postgres;
GRANT ALL ON SCHEMA public TO postgres;
GRANT ALL ON SCHEMA public TO PUBLIC;


--
-- PostgreSQL database dump complete
--

