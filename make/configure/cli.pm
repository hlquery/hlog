package make::configure::cli;

use v5.26.0;
use strict;
use warnings FATAL => qw(all);

use Exporter qw(import);

our @EXPORT_OK = qw(
    bool_flag
    parse_cli_options
    print_cli_help
);

sub bool_flag {
    my ($value, $default) = @_;
    return $default unless defined $value;
    return $value =~ /^(true|1|yes)$/i ? 'true' : 'false';
}

sub print_cli_help {
    print "\n";
    print "hlog configure options:\n";
    print "  --input=PATH            | Set the filein module path (default: run/logs/hlquery.log)\n";
    print "  --method=METHOD         | filein method (auto|inotify|refresh)\n";
    print "  --start-position=FLAG   | filein start_position (beginning|end)\n";
    print "  --refresh-ms=MS         | filein refresh interval when method=refresh\n";
    print "  --collection=NAME       | Target collection name for output_hlquery\n";
    print "  --endpoint=URL          | HLQuery HTTP endpoint for ingestion\n";
    print "  --auth-method=STYLE     | output_hlquery auth_method (bearer|api-key)\n";
    print "  --auth-token=TOKEN      | Optional auth token for hlquery output\n";
    print "  --environment=VALUE     | value added to the environment field filter\n";
    print "  --host=VALUE            | host_value emitted for each event\n";
    print "  --tags=VALUE            | tags_value emitted for each event\n";
    print "  --include-date=BOOL     | include_date event flag (true|false)\n";
    print "  --include-tags=BOOL     | include_tags event flag (true|false)\n";
    print "  --timeout=SECONDS       | output_hlquery timeout\n";
    print "  --failure-buffer=PATH   | File to store failed inserts for replay\n";
    print "  --help, -h              | Show this help message\n";
    print "\n";
}

sub parse_cli_options {
    my (%args) = @_;
    my $vars_ref = $args{vars} // die "parse_cli_options requires vars\n";

    while (my $arg = shift @ARGV) {
        if ($arg eq '--help' || $arg eq '-h') {
            print_cli_help();
            exit 0;
        } elsif ($arg =~ /^--input=(.+)$/) {
            $vars_ref->{HLOG_INPUT_PATH} = $1;
        } elsif ($arg eq '--input') {
            $vars_ref->{HLOG_INPUT_PATH} = shift @ARGV // die "--input requires a value\n";
        } elsif ($arg =~ /^--method=(.+)$/) {
            $vars_ref->{HLOG_INPUT_METHOD} = lc $1;
        } elsif ($arg eq '--method') {
            $vars_ref->{HLOG_INPUT_METHOD} = lc(shift @ARGV // die "--method requires a value\n");
        } elsif ($arg =~ /^--start-position=(.+)$/) {
            $vars_ref->{HLOG_INPUT_START} = lc $1;
        } elsif ($arg eq '--start-position') {
            $vars_ref->{HLOG_INPUT_START} = lc(shift @ARGV // die "--start-position requires a value\n");
        } elsif ($arg =~ /^--refresh-ms=(.+)$/) {
            $vars_ref->{HLOG_INPUT_REFRESH_MS} = $1;
        } elsif ($arg eq '--refresh-ms') {
            $vars_ref->{HLOG_INPUT_REFRESH_MS} = shift @ARGV // die "--refresh-ms requires a value\n";
        } elsif ($arg =~ /^--collection=(.+)$/) {
            $vars_ref->{HLOG_OUTPUT_COLLECTION} = $1;
        } elsif ($arg eq '--collection') {
            $vars_ref->{HLOG_OUTPUT_COLLECTION} = shift @ARGV // die "--collection requires a value\n";
        } elsif ($arg =~ /^--endpoint=(.+)$/) {
            $vars_ref->{HLOG_OUTPUT_ENDPOINT} = $1;
        } elsif ($arg eq '--endpoint') {
            $vars_ref->{HLOG_OUTPUT_ENDPOINT} = shift @ARGV // die "--endpoint requires a value\n";
        } elsif ($arg =~ /^--auth-method=(.+)$/) {
            $vars_ref->{HLOG_OUTPUT_AUTH_METHOD} = $1;
        } elsif ($arg eq '--auth-method') {
            $vars_ref->{HLOG_OUTPUT_AUTH_METHOD} = shift @ARGV // die "--auth-method requires a value\n";
        } elsif ($arg =~ /^--auth-token=(.+)$/) {
            $vars_ref->{HLOG_OUTPUT_AUTH_TOKEN} = $1;
        } elsif ($arg eq '--auth-token') {
            $vars_ref->{HLOG_OUTPUT_AUTH_TOKEN} = shift @ARGV // die "--auth-token requires a value\n";
        } elsif ($arg =~ /^--environment=(.+)$/) {
            $vars_ref->{HLOG_ENVIRONMENT} = $1;
        } elsif ($arg eq '--environment') {
            $vars_ref->{HLOG_ENVIRONMENT} = shift @ARGV // die "--environment requires a value\n";
        } elsif ($arg =~ /^--host=(.+)$/) {
            $vars_ref->{HLOG_HOST_VALUE} = $1;
        } elsif ($arg eq '--host') {
            $vars_ref->{HLOG_HOST_VALUE} = shift @ARGV // die "--host requires a value\n";
        } elsif ($arg =~ /^--tags=(.+)$/) {
            $vars_ref->{HLOG_TAGS_VALUE} = $1;
        } elsif ($arg eq '--tags') {
            $vars_ref->{HLOG_TAGS_VALUE} = shift @ARGV // die "--tags requires a value\n";
        } elsif ($arg =~ /^--include-date=(.+)$/) {
            $vars_ref->{HLOG_INCLUDE_DATE} = bool_flag($1, $vars_ref->{HLOG_INCLUDE_DATE});
        } elsif ($arg eq '--include-date') {
            $vars_ref->{HLOG_INCLUDE_DATE} = bool_flag(shift @ARGV // die "--include-date requires a value\n", $vars_ref->{HLOG_INCLUDE_DATE});
        } elsif ($arg =~ /^--include-tags=(.+)$/) {
            $vars_ref->{HLOG_INCLUDE_TAGS} = bool_flag($1, $vars_ref->{HLOG_INCLUDE_TAGS});
        } elsif ($arg eq '--include-tags') {
            $vars_ref->{HLOG_INCLUDE_TAGS} = bool_flag(shift @ARGV // die "--include-tags requires a value\n", $vars_ref->{HLOG_INCLUDE_TAGS});
        } elsif ($arg =~ /^--timeout=(.+)$/) {
            $vars_ref->{HLOG_OUTPUT_TIMEOUT} = $1;
        } elsif ($arg eq '--timeout') {
            $vars_ref->{HLOG_OUTPUT_TIMEOUT} = shift @ARGV // die "--timeout requires a value\n";
        } elsif ($arg =~ /^--failure-buffer=(.+)$/) {
            $vars_ref->{HLOG_FAILURE_BUFFER} = $1;
        } elsif ($arg eq '--failure-buffer') {
            $vars_ref->{HLOG_FAILURE_BUFFER} = shift @ARGV // die "--failure-buffer requires a value\n";
        } else {
            print "Unknown option: $arg\n";
            print_cli_help();
            exit 1;
        }
    }

    my %method_allow = map { $_ => 1 } qw(auto inotify notification refresh);
    unless ($method_allow{$vars_ref->{HLOG_INPUT_METHOD}}) {
        die "Invalid input method '$vars_ref->{HLOG_INPUT_METHOD}'\n";
    }

    my %start_allow = map { $_ => 1 } qw(beginning end);
    unless ($start_allow{$vars_ref->{HLOG_INPUT_START}}) {
        die "Invalid start_position '$vars_ref->{HLOG_INPUT_START}'\n";
    }
}

1;
