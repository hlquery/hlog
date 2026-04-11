#############################################################################
# hlog pipeline configuration
#
# hlog tails files and turns each appended line into a document.
# The runtime shape is:
#
#   filein -> filter_* -> output_*
#
# The defaults below are designed for a clean operator-facing pipeline
# config, but keep hlquery naming and collection behavior.
#
#############################################################################

# Event shaping
#
# These options control the fields that hlog emits for each log line.
# This is useful when you want more explicit field names or a different date
# format without recompiling.
#
# id_field         Document id field name.
# message_field    Field that receives the raw log line.
# path_field       Field name for the absolute file path.
# file_field       Field name for the basename of the watched file.
# host_field       Field name for the host value.
# tags_field       Field name for the tag value.
# date_field       Field name used when include_date="true".
# date_format      strftime format used for the date field.
# host_value       Static host value written into host_field.
# tags_value       Static tag value written into tags_field.
# include_*        Toggle whether a field is included at all.
#
# Examples:
#   date_format="%Y-%m-%dT%H:%M:%S"      -> 2026-03-09T17:25:00
#   date_format="%Y.%m.%d"               -> 2026.03.09
#   date_field="created_at"              -> custom timestamp field name
#   include_date="false"                 -> do not emit any date field

#<event id_field="id"
#       message_field="message"
#       path_field="source_path"
#       file_field="source_file"
#       host_field="host"
#       tags_field="tags"
#       date_field="observed_at"
#       date_format="%Y-%m-%dT%H:%M:%S"
#       host_value="${HLOG_HOST_VALUE}"
#       tags_value="${HLOG_TAGS_VALUE}"
#       include_path="true"
#       include_file="true"
#       include_host="true"
#       include_tags="${HLOG_INCLUDE_TAGS}"
#       include_date="${HLOG_INCLUDE_DATE}">

# Filter stage
#
# Adds a constant string field to every event.

#<filter_add_field field="pipeline" value="hlog">

#<filter_add_field field="source_kind" value="file">

#<filter_add_field field="environment" value="${HLOG_ENVIRONMENT}">

# Optional drop filter examples:
#
# <filter_drop_if_contains field="message" value="healthcheck">
# <filter_drop_if_contains field="message" value="DEBUG">
#
# Parse a JSON log line and merge its object keys into the event:
# <filter_json_parse field="message">
#
# Parse a JSON log line into a nested field instead of the root document:
# <filter_json_parse field="message" target_field="payload">
#
# Extract regex capture groups into named fields:
# <filter_regex_extract field="message"
#                       pattern="level=([A-Z]+) request_id=([a-z0-9-]+)"
#                       fields="level,request_id">
#
# Remove fields after enrichment:
# <filter_remove_field fields="message,source_path">

# Shared modules
#
# Source and extension modules now live in run/conf/modules.conf.

# Output stage: local stdout/log echo
#
# When enabled, each ingested line is written to the hlog runtime log.
# log_all_events="true" adds verbose pipeline event tracing to the
# hlog runtime log.

<output_stdout enabled="true" log_all_events="false">

<include file="modules.conf">
