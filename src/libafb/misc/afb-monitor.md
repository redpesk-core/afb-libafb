# API monitor

The API monitor is here to help to control
internal aspects of programs that use libafb.

Its presence and if present, its completeness
depend of the integration. This may change
depending of the development stage, the permission
and the integration policy.

This document present the full API of monitoring.

The available verbs are:

- **get**: Introspection of internals
- **set**: Change some settings
- **session**: Retrieve session data
- **subscribe**, **unsubscribe**: Handling of monitor events
- **trace**: Tracing internals

## Verb get

The verb `get` accepts one JSON object whose entries can be
any combination of:

- verbosity: for getting verbosity
- apis: for getting apis

The response is an object containing the matching
combination of keys above.

### Getting verbosities

The basic use is to pass as argument the list of queried API.

`get {"verbosity":["XXX","YYY",...]}` returns the verbosity
levels of the APIs whose names are listed.

Two names are specials:

- the empty name `""` matches the global level, the one that
isn't linked to any api.

- the star name `"*"` matches any API but not the global level.

The response is an object keyed at root by `"verbosity"` and
containing for each API its verbosity level keyed by the name
of the API. For the global verbosity level, the key is the
empty string.

The possible verbosity values are: "debug", "info", "notice", "warning", "error".

Example:

```sh
> afb-client -H localhost:2222/api monitor get '{"verbosity":true}'
ON-REPLY 1:monitor/get: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  },
  "response":{
    "verbosity":{
      "":"notice",
      "hello":"notice",
      "monitor":"notice",
      "fakename":"notice"
    }
  }
}
```

Other uses are possible as is described below:

`get {"verbosity":false}` has no effect.

`get {"verbosity":true}` is synonym of `get {"verbosity":["","*"]}`.

`get {"verbosity":"XXX"}` is synonym of `get {"verbosity":["XXX"]}`.

`get {"verbosity":{"XXX":true, "YYY": ...}}` is synonym
of `get {"verbosity":["XXX", "YYY", ...]}`, the array made
of keys whose values are not false or null.

### Gettting APIs

When getting API you can get just a list of the names of available APIS
or the full available description of the chosen APIs.

#### Getting the list of the available APIs

The list of available APIs is return ned by submitting one of the
queries `get {"apis":"*"}` or `get {"apis":false}`.

The response is an object keyed at root by `"apis"` and
containing for each API its aliasing status keyed by the name
of the API. The alias status of the API is either `true` for
a plain API or the name of the aliased API as a string.

Exemple:

```sh
> afb-client -H localhost:1234/api monitor get '{"apis":false}'
ON-REPLY 1:monitor/get: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  },
  "response":{
    "apis":{
      "hello":true,
      "monitor":true,
      "fakename":"hello"
    }
  }
}
```

#### Getting description of APIs

The basic use is to pass as argument the list of queried API.

`get {"apis":["XXX","YYY",...]}` returns the description
of the APIs whose names are listed.

The response is an object keyed at root by `"apis"` and
containing for each API that provides it, its description
as a JSON object.

Exemple:

```sh
> afb-client -H localhost:1234/api monitor get '{"apis":true}'
ON-REPLY 1:monitor/get: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  },
  "response":{
    "apis":{
      "hello":{
        "openapi":"3.0.0",
        "info":{
          "version":"0.0.0",
          "title":"hello"
        },
        ...
```

Other uses are possible as is described below:

`get {"apis":true}` for getting description of all available APIs.

`get {"apis":"XXX"}` is synonym of `get {"apis":["XXX"]}`.

`get {"apis":{"XXX":true, "YYY": ...}}` is synonym
of `get {"apis":["XXX", "YYY", ...]}`, the array made
of keys whose values are not false or null.

## Verb set

The verb `set` accepts one JSON object whose entries can be
any combination of:

- verbosity: for setting verbosity
- subscribe: for subscribing to events
- unsubscribe: for unsubscribing to events

The response is just a status.

### Setting subscribe or unsuscribe

The request `set {"subscribe":...}` is a synonym of `subscribe ...`
and same thing for `unsubscribe`. The advantage here is that it is
possible to subscribe and unsubscribe in the same request.

### Setting verbosity

The basic use is to pass as argument an object whose keys are the
APIs and whose values are the verbosity level to set:
`set {"verbosity":{"XXX":"error","YYY":"debug",...}}`.

The possible verbosity values are: "debug", "info", "notice", "warning", "error".

As for setting, the 2 following API names are special:

- `""`: the empty name set the global verbosity
- `"*"`: any API

Exemple:

```sh
> afb-client -H localhost:1234/api monitor set '{"verbosity":{"monitor":"debug","hello":"info"}}'
ON-REPLY 1:monitor/set: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  }
}
```

Other uses are possible as is described below:

`set {"verbosity":"XXX"}` is a synonym of `set {"verbosity":{"":"XXX","*":"XXX"}`


## Verb session

The verb `session` takes no argument and returns the
data of the session.

Example:

```sh
> afb-client -H localhost:1234/api monitor session null
ON-REPLY 1:monitor/session: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  },
  "response":{
    "uuid":"65dc903f-548b-429e-8b42-14baf06a9007",
    "timeout":32000000,
    "remain":32000000
  }
}
```

The value returned is an object with fields:

- **uuid**: identifier of the current session
- **timeout**: time out of the session in seconds
- **remain**: remaining time before expiration in seconds

## Verbs subscribe and unsubscribe

This verbs are used for subscribing (and unsubscribing) to
events generated by the monitoring. It accepts an array
of strings or just a single string, one string by event
subscribed or unsubscribed.

Example:

```sh
> afb-client -H localhost:1234/api monitor subscribe disconnected
ON-REPLY 1:monitor/subscribe: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  }
}
```

At this time only one event is generated by the monitoring.

### Monitoring event

The event `disconnected` is generated when an API is disconnected
from its server implementation (it happens in the microservices world!).

The event carries one string: the name of the disconnected API.

Example:

```sh
{
  "jtype":"afb-event",
  "event":"monitor/disconnected",
  "data":"hello"
}
```

## Verb trace

The verb `trace` accepts one JSON object whose entries can be
any combination of:

- add: for adding a trace
- drop: for removing a trace

The response is just a status.

Traces are subject to restrictions: normally, it is only possible to trace
its own session.

### Adding a trace

The argument of `trace add` is an array of trace description object
or just a single trace description object.

A trace description object is a combination of the keys below:

- `name`: Name of the trace event (defaults to "trace" if unset)
- `tag`: Name of the tag associated to the query (defaults to "trace" if unset)
- `apiname`: The name of the API to trace, or * (or unset) for all APIs
- `verbname`: The name of the verbs to trace, or * (or unset) for all verbs
- `uuid`: The UUId of the sessions to trace, or * (or unset) for all sessions
- `pattern`: Pattern of the event to trace or * (or unset) for all events
- `api`: Array of strings or single string for api required traces.
   Valid values are:
   "add_alias",  "all",  "api_add_verb",  "api",  "api_del_verb",
   "api_seal",  "api_set_on_event",  "api_set_on_init",  "api_set_verbs",
   "call",  "callsync",  "abort_job",  "class_provide",  "class_require",
   "common",  "delete_api",  "event",  "event_broadcast",  "event_handler_add",
   "event_handler_del",  "event_make",  "extra",  "get_event_loop",
   "get_system_bus",  "get_user_bus",  "new_api",  "on_event",
   "on_event_handler",  "post_job",  "require_api",  "rootdir_get_fd",
   "rootdir_open_locale",  "settings",  "start",  "unshare_session",  "vverbose"
- `request`: Array of strings or single string for request required traces.
   Valid values are:
   "addref", "all", "args", "begin", "common", "context", "context_drop",
   "context_get", "context_getinit", "context_make", "context_set", "end",
   "event", "extra", "get", "get_application_id", "get_client_info", "get_uid",
   "has_permission", "interface", "json", "life", "ref", "reply", "security",
   "session", "session_close", "session_set_LOA", "session_get_LOA", "subcall",
   "subcall_result", "subcalls", "subcallsync", "subcallsync_result", "subscribe",
   "unref", "unsubscribe", "userdata", "vverbose"
- `event`: Array of strings or single string for event required traces.
   Valid values are:
   "addref", "all", "broadcast_after", "broadcast_before", "common", "create",
   "extra", "name", "push_after", "push_before", "unref"
- `session`: Array of strings or single string for session required traces.
   Valid values are:
   "addref", "all", "close", "common", "create", "destroy", "unref"
- `global`: Array of strings or single string for global required traces.
   Valid values are:
   "all", "vverbose"
- `for`: An array of trace description objects that are evaluated using the
   current setting. Can be nested. Intended to reduce size of queries by
   factorizing common terms.

The API also understand single request settings, i.e. the query
`trace {"add":"all"}` is synonym to `trace {"add":{"request":"all"}}`
that is synonym to  `trace {"add":[{"apiname":"*","verbname":"*","request":"all"}]}`.

Example:

```sh
> afb-client -H localhost:1234/api
monitor trace {"add":{"tag":"TAG","api":"hello","request":"all"}}
ON-REPLY 1:monitor/trace: OK
{
  "jtype":"afb-reply",
  "request":{
    "status":"success",
    "code":0
  }
}
...

```


### Dropping a trace

The argument of `trace drop` is either a boolean or an object
having any combination of:

- event: for removing traces of events
- tag: for removing traces of given tags
- uuid: for removing traces of sessions

Each of this keys accept array of strings or single string.

Example:

```sh
afb-client -H localhost:1234/api monitor trace '{"drop":{"tag":"TAG"}}'
```

When a boolean is given and is true, `trace {"drop":true}` all
existing traces are dropped away.

