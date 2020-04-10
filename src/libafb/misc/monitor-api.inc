
static const char _afb_description_monitor[] =
    "{\"openapi\":\"3.0.0\",\"info\":{\"description\":\"monitoring of binding"
    "s and internals\",\"title\":\"monitor\",\"version\":\"1.0\",\"x-binding-"
    "c-generator\":{\"api\":\"monitor\",\"version\":2,\"prefix\":\"f_\",\"pos"
    "tfix\":\"\",\"preinit\":null,\"init\":null,\"onevent\":null,\"scope\":\""
    "static\",\"private\":true}},\"servers\":[{\"url\":\"ws://{host}:{port}/a"
    "pi/monitor\",\"description\":\"The API server.\",\"variables\":{\"host\""
    ":{\"default\":\"localhost\"},\"port\":{\"default\":\"1234\"}},\"x-afb-ev"
    "ents\":[{\"$ref\":\"#/components/schemas/afb-event\"}]}],\"components\":"
    "{\"schemas\":{\"afb-reply\":{\"$ref\":\"#/components/schemas/afb-reply-v"
    "1\"},\"afb-event\":{\"$ref\":\"#/components/schemas/afb-event-v1\"},\"af"
    "b-reply-v1\":{\"title\":\"Generic response.\",\"type\":\"object\",\"requ"
    "ired\":[\"jtype\",\"request\"],\"properties\":{\"jtype\":{\"type\":\"str"
    "ing\",\"const\":\"afb-reply\"},\"request\":{\"type\":\"object\",\"requir"
    "ed\":[\"status\"],\"properties\":{\"status\":{\"type\":\"string\"},\"inf"
    "o\":{\"type\":\"string\"},\"token\":{\"type\":\"string\"},\"uuid\":{\"ty"
    "pe\":\"string\"},\"reqid\":{\"type\":\"string\"}}},\"response\":{\"type\""
    ":\"object\"}}},\"afb-event-v1\":{\"type\":\"object\",\"required\":[\"jty"
    "pe\",\"event\"],\"properties\":{\"jtype\":{\"type\":\"string\",\"const\""
    ":\"afb-event\"},\"event\":{\"type\":\"string\"},\"data\":{\"type\":\"obj"
    "ect\"}}},\"set-verbosity\":{\"anyOf\":[{\"$ref\":\"#/components/schemas/"
    "verbosity-map\"},{\"$ref\":\"#/components/schemas/verbosity-level\"}]},\""
    "get-request\":{\"type\":\"object\",\"properties\":{\"verbosity\":{\"$ref"
    "\":\"#/components/schemas/get-verbosity\"},\"apis\":{\"$ref\":\"#/compon"
    "ents/schemas/get-apis\"}}},\"get-response\":{\"type\":\"object\",\"prope"
    "rties\":{\"verbosity\":{\"$ref\":\"#/components/schemas/verbosity-map\"}"
    ",\"apis\":{\"type\":\"object\"}}},\"get-verbosity\":{\"anyOf\":[{\"type\""
    ":\"boolean\"},{\"type\":\"array\",\"items\":{\"type\":\"string\"}},{\"ty"
    "pe\":\"object\"}]},\"get-apis\":{\"anyOf\":[{\"type\":\"boolean\"},{\"ty"
    "pe\":\"array\",\"items\":{\"type\":\"string\"}},{\"type\":\"object\"}]},"
    "\"verbosity-map\":{\"type\":\"object\",\"patternProperties\":{\"^.*$\":{"
    "\"$ref\":\"#/components/schemas/verbosity-level\"}}},\"verbosity-level\""
    ":{\"enum\":[\"debug\",3,\"info\",2,\"notice\",\"warning\",1,\"error\",0]"
    "},\"trace-add\":{\"anyOf\":[{\"type\":\"array\",\"items\":{\"$ref\":\"#/"
    "components/schemas/trace-add-object\"}},{\"$ref\":\"#/components/schemas"
    "/trace-add-any\"}]},\"trace-add-any\":{\"anyOf\":[{\"$ref\":\"#/componen"
    "ts/schemas/trace-add-request\"},{\"$ref\":\"#/components/schemas/trace-a"
    "dd-object\"}]},\"trace-add-object\":{\"type\":\"object\",\"properties\":"
    "{\"name\":{\"type\":\"string\",\"description\":\"name of the generated e"
    "vent\",\"default\":\"trace\"},\"tag\":{\"type\":\"string\",\"description"
    "\":\"tag for grouping traces\",\"default\":\"trace\"},\"api\":{\"type\":"
    "\"string\",\"description\":\"api for requests, daemons and services\"},\""
    "verb\":{\"type\":\"string\",\"description\":\"verb for requests\"},\"uui"
    "d\":{\"type\":\"string\",\"description\":\"uuid of session for requests\""
    "},\"pattern\":{\"type\":\"string\",\"description\":\"pattern for events\""
    "},\"request\":{\"$ref\":\"#/components/schemas/trace-add-request\"},\"da"
    "emon\":{\"$ref\":\"#/components/schemas/trace-add-daemon\"},\"service\":"
    "{\"$ref\":\"#/components/schemas/trace-add-service\"},\"event\":{\"$ref\""
    ":\"#/components/schemas/trace-add-event\"},\"session\":{\"$ref\":\"#/com"
    "ponents/schemas/trace-add-session\"},\"for\":{\"$ref\":\"#/components/sc"
    "hemas/trace-add\"}},\"examples\":[{\"tag\":\"1\",\"for\":[\"common\",{\""
    "api\":\"xxx\",\"request\":\"*\",\"daemon\":\"*\",\"service\":\"*\"}]}]},"
    "\"trace-add-request\":{\"anyOf\":[{\"type\":\"array\",\"items\":{\"$ref\""
    ":\"#/components/schemas/trace-request-names\"}},{\"$ref\":\"#/components"
    "/schemas/trace-request-names\"}]},\"trace-request-names\":{\"title\":\"n"
    "ame of traceable items of requests\",\"enum\":[\"*\",\"addref\",\"all\","
    "\"args\",\"begin\",\"common\",\"context\",\"context_get\",\"context_set\""
    ",\"end\",\"event\",\"extra\",\"fail\",\"get\",\"json\",\"life\",\"ref\","
    "\"result\",\"session\",\"session_close\",\"session_set_LOA\",\"simple\","
    "\"store\",\"stores\",\"subcall\",\"subcall_result\",\"subcalls\",\"subca"
    "llsync\",\"subcallsync_result\",\"subscribe\",\"success\",\"unref\",\"un"
    "store\",\"unsubscribe\",\"vverbose\"]},\"trace-add-daemon\":{\"anyOf\":["
    "{\"type\":\"array\",\"items\":{\"$ref\":\"#/components/schemas/trace-dae"
    "mon-names\"}},{\"$ref\":\"#/components/schemas/trace-daemon-names\"}]},\""
    "trace-daemon-names\":{\"title\":\"name of traceable items of daemons\",\""
    "enum\":[\"*\",\"all\",\"common\",\"event_broadcast_after\",\"event_broad"
    "cast_before\",\"event_make\",\"extra\",\"get_event_loop\",\"get_system_b"
    "us\",\"get_user_bus\",\"queue_job\",\"require_api\",\"require_api_result"
    "\",\"rootdir_get_fd\",\"rootdir_open_locale\",\"unstore_req\",\"vverbose"
    "\"]},\"trace-add-service\":{\"anyOf\":[{\"type\":\"array\",\"items\":{\""
    "$ref\":\"#/components/schemas/trace-service-names\"}},{\"$ref\":\"#/comp"
    "onents/schemas/trace-service-names\"}]},\"trace-service-names\":{\"title"
    "\":\"name of traceable items of services\",\"enum\":[\"*\",\"all\",\"cal"
    "l\",\"call_result\",\"callsync\",\"callsync_result\",\"on_event_after\","
    "\"on_event_before\",\"start_after\",\"start_before\"]},\"trace-add-event"
    "\":{\"anyOf\":[{\"type\":\"array\",\"items\":{\"$ref\":\"#/components/sc"
    "hemas/trace-event-names\"}},{\"$ref\":\"#/components/schemas/trace-event"
    "-names\"}]},\"trace-event-names\":{\"title\":\"name of traceable items o"
    "f events\",\"enum\":[\"*\",\"all\",\"broadcast_after\",\"broadcast_befor"
    "e\",\"common\",\"create\",\"drop\",\"extra\",\"name\",\"push_after\",\"p"
    "ush_before\"]},\"trace-add-session\":{\"anyOf\":[{\"type\":\"array\",\"i"
    "tems\":{\"$ref\":\"#/components/schemas/trace-session-names\"}},{\"$ref\""
    ":\"#/components/schemas/trace-session-names\"}]},\"trace-session-names\""
    ":{\"title\":\"name of traceable items for sessions\",\"enum\":[\"*\",\"a"
    "ddref\",\"all\",\"close\",\"common\",\"create\",\"destroy\",\"renew\",\""
    "unref\"]},\"trace-drop\":{\"anyOf\":[{\"type\":\"boolean\"},{\"type\":\""
    "object\",\"properties\":{\"event\":{\"anyOf\":[{\"type\":\"string\"},{\""
    "type\":\"array\",\"items\":\"string\"}]},\"tag\":{\"anyOf\":[{\"type\":\""
    "string\"},{\"type\":\"array\",\"items\":\"string\"}]},\"uuid\":{\"anyOf\""
    ":[{\"type\":\"string\"},{\"type\":\"array\",\"items\":\"string\"}]}}}]}}"
    "},\"paths\":{\"/get\":{\"description\":\"Get monitoring data.\",\"get\":"
    "{\"x-permissions\":{\"session\":\"check\"},\"parameters\":[{\"in\":\"que"
    "ry\",\"name\":\"verbosity\",\"required\":false,\"schema\":{\"$ref\":\"#/"
    "components/schemas/get-verbosity\"}},{\"in\":\"query\",\"name\":\"apis\""
    ",\"required\":false,\"schema\":{\"$ref\":\"#/components/schemas/get-apis"
    "\"}}],\"responses\":{\"200\":{\"description\":\"A complex object array r"
    "esponse\",\"content\":{\"application/json\":{\"schema\":{\"$ref\":\"#/co"
    "mponents/schemas/afb-reply\"}}}}}}},\"/set\":{\"description\":\"Set moni"
    "toring actions.\",\"get\":{\"x-permissions\":{\"session\":\"check\"},\"p"
    "arameters\":[{\"in\":\"query\",\"name\":\"verbosity\",\"required\":false"
    ",\"schema\":{\"$ref\":\"#/components/schemas/set-verbosity\"}}],\"respon"
    "ses\":{\"200\":{\"description\":\"A complex object array response\",\"co"
    "ntent\":{\"application/json\":{\"schema\":{\"$ref\":\"#/components/schem"
    "as/afb-reply\"}}}}}}},\"/trace\":{\"description\":\"Set monitoring actio"
    "ns.\",\"get\":{\"x-permissions\":{\"session\":\"check\"},\"parameters\":"
    "[{\"in\":\"query\",\"name\":\"add\",\"required\":false,\"schema\":{\"$re"
    "f\":\"#/components/schemas/trace-add\"}},{\"in\":\"query\",\"name\":\"dr"
    "op\",\"required\":false,\"schema\":{\"$ref\":\"#/components/schemas/trac"
    "e-drop\"}}],\"responses\":{\"200\":{\"description\":\"A complex object a"
    "rray response\",\"content\":{\"application/json\":{\"schema\":{\"$ref\":"
    "\"#/components/schemas/afb-reply\"}}}}}}},\"/session\":{\"description\":"
    "\"describes the session.\",\"get\":{\"x-permissions\":{\"session\":\"che"
    "ck\"},\"parameters\":[{\"in\":\"query\",\"name\":\"refresh-token\",\"req"
    "uired\":false,\"schema\":{\"type\":\"boolean\"}}],\"responses\":{\"200\""
    ":{\"description\":\"A complex object array response\",\"content\":{\"app"
    "lication/json\":{\"schema\":{\"$ref\":\"#/components/schemas/afb-reply\""
    "}}}}}}}}}"
;

static void f_get(afb_req_t req);
static void f_set(afb_req_t req);
static void f_trace(afb_req_t req);
static void f_session(afb_req_t req);

static const struct afb_verb_v3 _afb_verbs_monitor[] = {
    {
        .verb = "get",
        .callback = f_get,
        .auth = NULL,
        .info = "Get monitoring data.",
        .session = AFB_SESSION_CHECK,
        .vcbdata = NULL,
        .glob = 0
    },
    {
        .verb = "set",
        .callback = f_set,
        .auth = NULL,
        .info = "Set monitoring actions.",
        .session = AFB_SESSION_CHECK,
        .vcbdata = NULL,
        .glob = 0
    },
    {
        .verb = "trace",
        .callback = f_trace,
        .auth = NULL,
        .info = "Set monitoring actions.",
        .session = AFB_SESSION_CHECK,
        .vcbdata = NULL,
        .glob = 0
    },
    {
        .verb = "session",
        .callback = f_session,
        .auth = NULL,
        .info = "describes the session.",
        .session = AFB_SESSION_CHECK,
        .vcbdata = NULL,
        .glob = 0
    },
    {
        .verb = NULL,
        .callback = NULL,
        .auth = NULL,
        .info = NULL,
        .session = 0,
        .vcbdata = NULL,
        .glob = 0
	}
};

static const struct afb_binding_v3 _afb_binding_monitor = {
    .api = "monitor",
    .specification = _afb_description_monitor,
    .info = "monitoring of bindings and internals",
    .verbs = _afb_verbs_monitor,
    .preinit = NULL,
    .init = NULL,
    .onevent = NULL,
    .userdata = NULL,
    .noconcurrency = 0
};

