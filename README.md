<div align="center">
  <img src="https://docs.hlquery.com/img/hlquery/2.png" alt="hlog logo" width="200">
</div>

<div align="center">

**hlog, a modular data feeder for hlquery.**

[![Follow hlquery](https://img.shields.io/badge/Follow-%40hlquery-blue?logo=x&logoColor=white&labelColor=000000)](https://x.com/hlquery)

[![GitHub](https://img.shields.io/badge/GitHub-hlog-blue?logo=github&logoColor=white&labelColor=000000)](https://github.com/hlquery/hlog/)
[![Linux Build](https://img.shields.io/badge/Linux%20Build-passing-brightgreen?logo=linux&logoColor=white&labelColor=000000)](https://github.com/hlquery/hlog/actions)
[![macOS Build](https://img.shields.io/badge/macOS%20Build-passing-brightgreen?logo=apple&logoColor=white&labelColor=000000)](https://github.com/hlquery/hlog/actions)
[![FreeBSD Build](https://img.shields.io/badge/FreeBSD%20Build-passing-brightgreen?logo=freebsd&logoColor=white&labelColor=000000)](https://github.com/hlquery/hlog/actions)
[![License](https://img.shields.io/badge/License-BSD%203--Clause-a35a0f?logo=open-source-initiative&logoColor=white&labelColor=000000)](https://opensource.org/licenses/BSD-3-Clause)

</div>

hlog is a lightweight C++ data feeder for hlquery. It ingests external data, transforms it in flight, and forwards structured events into an hlquery collection.

The design is intentionally modular so pipelines stay small, composable, and easy to adapt to different inputs, enrichment steps, and destinations.

Build:

```bash
$ cd etc/hlog
$ ./configure
$ make
```

On macOS and the BSDs, use `gmake` instead of the platform `make`.

Run in foreground (for debugging):

```bash
$ ./run/hlquery start --nofork
```

Status and stop:

```bash
$ ./run/hlog status
$ ./run/hlog stop
```

The wrapper in `run/hlog` is the normal entrypoint. It handles background start, pidfile management, status, stop, restart, and JSON wrapper output. `build/bin/hlog` is the underlying binary, and the runtime cleanup path removes the daemon pidfile on clean exit.

On Windows, the C++ binary uses the refresh polling path. The wrapper supports foreground execution and config testing, but background wrapper commands such as `start` without `--nofork`, `stop`, `status`, and `restart` are still not available.

Watcher methods:

- `inotify`: use kernel file notifications on Linux
- `refresh`: scan files every `refresh_ms` interval
- `auto`: choose `inotify` when available, otherwise refresh

`run/conf/hlog.conf` defines the pipeline. Like `hlquery.conf`, file paths can be relative and are resolved from the directory containing `hlog.conf`. The hlquery server config is not declared inside `hlog.conf`; `hlog` uses `--config` or its built-in default path. Runtime modules are loaded from `run/conf/modules.conf`, which is included from `hlog.conf`. The default input is a self-contained sample file at `run/tests/file.txt`, created by `make prepare` with a `hello world!` line:

```conf
<hlog>

<input_file path="tests/file.txt"
            start_position="end"
            method="auto">

<event message_field="message"
       path_field="source_path"
       file_field="source_file"
       date_field="observed_at"
       date_format="%Y-%m-%dT%H:%M:%S"
       tags_value="log_line"
       include_date="true">

<filter_add_field field="pipeline" value="hlog">

<filter_add_field field="source_kind" value="file">

<output_stdout enabled="true">

<output_hlquery enabled="true"
                endpoint="http://127.0.0.1:9200"
                collection="logs"
                auth_method="bearer"
                auth_token=""
                timeout="5">
```

Supported stages:

- `input_file`: tail a file, with `start_position="beginning|end"`
- `input_file method="inotify"`: use kernel file notifications on Linux
- `input_file method="refresh"`: use periodic scanning, with `refresh_ms="1000"` style intervals
- `input_file method="auto"`: choose `inotify` on Linux, otherwise polling
- `event`: configure emitted document field names and date formatting
- `filter_add_field`: add constant fields to every event
- `filter_json_parse`: parse a JSON string field and merge it into the event or store it under `target_field`
- `filter_regex_extract`: extract regex capture groups into named fields
- `filter_drop_if_contains`: drop events when a field contains a substring
- `filter_remove_field`: delete fields after enrichment
- `module`: load one shared object that can inspect, mutate, or drop events
- `output_stdout`: print events
- `output_hlquery`: post events into an hlquery collection

If no `input_file` stages are configured, `hlog` falls back to the file log targets defined in `hlquery.conf`.

Default emitted fields are `id`, `message`, `path`, `file`, `host`, `ingested_at`, and `tags`. The `<event>` tag lets you rename those fields or disable some of them entirely.

The collection name is configured in `<output_hlquery collection="logs">`.

Useful `<event>` examples:

```conf
<event message_field="line"
       date_field="created_at"
       date_format="%Y.%m.%d"
       include_host="false">
```

```conf
<event message_field="raw"
       include_date="false"
       include_tags="false">
```

Useful filter examples:

```conf
<filter_json_parse field="message">
```

```conf
<filter_regex_extract field="message"
                      pattern="level=([A-Z]+) request_id=([a-z0-9-]+)"
                      fields="level,request_id">

<filter_remove_field fields="message">
```

### Shared modules

`hlog` can load standalone shared objects during pipeline startup. The runtime ABI is intentionally small and lives in:

- `include/core/modules.h`
- `include/core/modulemanager.h`
- `include/core/hlcore.h`

Load one module with:

```conf
<module name="debug">
```

Then configure it with a matching tag in `run/conf/modules.conf`, for example `debug`, `irc`, or `irc_connect`.

`hlog` looks for `<name>.dylib` and `m_<name>.dylib` on macOS, `<name>.so` and `m_<name>.so` on Linux and BSD, and `<name>.dll` and `m_<name>.dll` on Windows, under `run/modules` and `build/modules`.

The default build now compiles sample modules from `src/modules/*.cpp` into `build/modules/*.dylib` on macOS, `build/modules/*.so` on Linux/BSD, and `build/modules/*.dll` on Windows. The included examples are:

- `src/modules/m_filein.cpp`
- `src/modules/m_irc.cpp`
- `src/modules/m_redis.cpp`

IRC bridge example:

```conf
<module name="irc">
```

The IRC module is non-blocking relative to the main pipeline loop: it queues lines and sends them from a background worker thread with reconnect handling.

Redis source example:

```conf
<module name="redis">
```

```conf
<redis_connect
        host="127.0.0.1"
        port="6379"
        channel="logs"
        collection="redis-logs"
        batch_interval="10s"
        reconnect_ms="5000">
```

