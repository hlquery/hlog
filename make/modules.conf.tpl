# Runtime-loadable hlog modules
#
# This file controls which shared modules hlog loads at startup and the
# per-module configuration blocks they read.
#
# Loading works like hlquery:
# a module entry such as:
#   <module name="redis">
# tells hlog to try to load:
#   run/modules/m_redis${HLOG_MODULE_SUFFIX}
# or:
#   build/modules/m_redis${HLOG_MODULE_SUFFIX}
# On macOS, the default suffix is .dylib. On Windows, it is .dll.
#
# Add or remove one <module name="..."> line to turn a module on or off.
# Module-specific settings live in a matching config block below.

# ----------------------------------------------------------------------
# TEMPLATES
# ----------------------------------------------------------------------
# Reusable output destination references.
#
# Use template="name" on output_hlquery or redis_connect to inherit
# endpoint/auth/collection timeout settings. Local attributes override the
# template values.

<template name="file_logs_target"
     endpoint="${HLOG_OUTPUT_ENDPOINT}"
     collection="${HLOG_OUTPUT_COLLECTION}"
     auth_method="${HLOG_OUTPUT_AUTH_METHOD}"
     auth_token="${HLOG_OUTPUT_AUTH_TOKEN}"
     timeout="${HLOG_OUTPUT_TIMEOUT}">

# ----------------------------------------------------------------------
# FILE INPUT MODULE
# ----------------------------------------------------------------------
# Source module that tails one or more files and feeds lines into hlog.
#
# path             File to follow. Use commas for multiple paths.
# start_position   beginning | end
#                  beginning -> read existing file contents first
#                  end       -> tail only newly appended lines
# method           auto | inotify | refresh
#                  inotify      -> kernel file notifications on Linux
#                  refresh      -> periodic stat/read scan
# refresh_ms       Poll interval in milliseconds when method="refresh"
# posted_to        Events from filein are sent by output_hlquery below.
#                  Set endpoint/collection there to choose the target.
# batch_lines      Optional max lines per posted document.
#                  0 disables line-count batching.
# batch_interval   Optional timed flush window for file batches.
#                  Works like Redis batch_interval for file input posting.
#                  Examples: 10s, 1m, 10d
#                  0 posts each line immediately unless batch_lines triggers.
#
# Leave path empty to fall back to file log targets from hlquery.conf.

<module name="filein">

<filein_connect
     template="file_logs_target"
     path="${HLOG_INPUT_PATH}"
     start_position="${HLOG_INPUT_START}"
     method="${HLOG_INPUT_METHOD}"
     refresh_ms="${HLOG_INPUT_REFRESH_MS}"
     batch_lines="0"
     batch_interval="0">

# ----------------------------------------------------------------------
# REDIS MODULE
# ----------------------------------------------------------------------
# Source module that subscribes to one Redis pub/sub channel and feeds each
# published message through the normal hlog pipeline and hlquery output.
#
# Uncomment to load it:
#
#<module name="redis">
#
# Redis runtime settings:
# host           Redis hostname
# port           Redis TCP port
# channel        Pub/sub channel to subscribe to
# password       Optional AUTH password
# username       Optional AUTH username for ACL-enabled Redis
# db             Optional SELECT database number before subscribing
# label          Optional event label; defaults to redis:<channel>
# reconnect_ms   Delay between reconnect attempts
# endpoint       Optional hlquery HTTP endpoint for pipeline output
# collection     Optional hlquery collection name for ingested messages
# auth_method    bearer | api-key for hlquery output
# auth_token     Optional auth token for hlquery output
# timeout        Socket timeout in seconds for Redis and hlquery output
# batch_lines    Optional max Redis messages per posted document batch
# batch_interval Timed batch flush window for hlquery output
#                Examples: 0, 10s, 1m

<redis_connect
     host="127.0.0.1"
     port="6379"
     channel="logs"
     label="redis:logs"
     template="file_logs_target"
     batch_lines="0"
     batch_interval="10s"
     reconnect_ms="5000">
