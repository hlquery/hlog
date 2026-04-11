# Runtime-loadable hlog modules
#
# This file controls which shared modules hlog loads at startup and the
# per-module configuration blocks they read.
#
# Loading works like hlquery:
# a module entry such as:
#   <module name="irc">
# tells hlog to try to load:
#   run/modules/m_irc${HLOG_MODULE_SUFFIX}
# or:
#   build/modules/m_irc${HLOG_MODULE_SUFFIX}
# On macOS, the default suffix is .dylib. On Windows, it is .dll.
#
# Add or remove one <module name="..."> line to turn a module on or off.
# Module-specific settings live in a matching config block below.

# ----------------------------------------------------------------------
# TEMPLATES
# ----------------------------------------------------------------------
# Reusable output destination references.
#
# Use template="name" on output_hlquery or irc_connect to inherit
# endpoint/auth/collection timeout settings. Local attributes override the
# template values.

<template name="file_logs_target"
     endpoint="${HLOG_OUTPUT_ENDPOINT}"
     collection="${HLOG_OUTPUT_COLLECTION}"
     auth_method="${HLOG_OUTPUT_AUTH_METHOD}"
     auth_token="${HLOG_OUTPUT_AUTH_TOKEN}"
     timeout="${HLOG_OUTPUT_TIMEOUT}">

<template name="irc_logs_target"
     endpoint="${HLOG_OUTPUT_ENDPOINT}"
     collection="irc-{%Y.%m}"
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
#                  Works like IRC post_interval for file input posting.
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
# IRC MODULE
# ----------------------------------------------------------------------
# Non-blocking IRC bridge that sends log lines to one channel using a
# background worker thread.
#
# Uncomment to load it:
#
#<module name="irc">
#
# IRC runtime settings:
# server       IRC server hostname
# port         IRC server port
# channel      Channel to join
# nick         Nickname used by the bot
# user         IRC USER field (optional, defaults to nick)
# realname     IRC realname field
# pass         Optional IRC server password
# reconnect_ms Delay between reconnect attempts
# queue_limit  Max buffered lines before oldest entries are dropped
# endpoint     Optional hlquery HTTP endpoint for IRC activity posts
# collection   Optional activity collection, supports logs-{%Y.%m} style braces
# auth_method  bearer | api-key for activity posts
# auth_token   Optional auth token for activity posts
# timeout      Socket timeout in seconds for activity posts
# post_interval Batch window for activity posts: 0, 10s, 1m, 10m
#              0 posts each event immediately
# post_privmsgs true | false
#              Post channel PRIVMSG lines into hlquery
# post_channels true | false
#              Post joins, parts, topic changes, kicks and notices
# post_connect true | false
#              Post server/connect lifecycle events
# log_all_irc_events true | false
#              Emit verbose IRC module runtime traces into the hlog log

<irc_connect
     server="irc.libera.chat"
     port="6667"
     channel="#ubuntu"
     nick="blabla"
     user="blabla"
     realname="hlog irc bridge"
     template="irc_logs_target"
     post_interval="10s"
     post_privmsgs="true"
     post_channels="true"
     post_connect="true"
     log_all_irc_events="false"
     reconnect_ms="5000"
     queue_limit="1000">

# IRC local module log settings.
#
# logging            true | false
#                    Controls only the local run/logs/irc.log file.
# log_retention_days Retention window in days for rotated local IRC logs.

<irc_log
     logging="true"
     log_retention_days="3">

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
