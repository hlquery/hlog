#!/usr/bin/env perl
#
# hlog - Live log pipeline for hlquery.
#

use strict;
use warnings;
use File::Spec;
use File::Basename qw(dirname);
use Cwd qw(abs_path);
use POSIX qw(strftime WNOHANG);

my $VERSION = "${HLQUERY_VERSION}";
my $BINARY_NAME = "hlog";
my $DEFAULT_HLOG_CONFIG = File::Spec->catfile("${HLOG_BASE_DIR}", "run", "conf", "hlog.conf");
my $DEFAULT_PIDFILE = File::Spec->catfile("${HLOG_BASE_DIR}", "run", "pid", "hlog.pid");
my $DEFAULT_LOGFILE = File::Spec->catfile("${HLOG_BASE_DIR}", "run", "logs", "hlog.log");
my $IS_WINDOWS = ($^O eq 'MSWin32') ? 1 : 0;
my $EXE_SUFFIX = $IS_WINDOWS ? '.exe' : '';

my $RED = "\033[31m";
my $GREEN = "\033[32m";
my $BLUE = "\033[34m";
my $YELLOW = "\033[33m";
my $BLACK = "\033[30m";
my $BOLD = "\033[1m";
my $RESET = "\033[0m";
my $json_output = 0;

sub print_usage {
    print <<"EOF";
Usage: $0 <command> [options]

Commands:

    start       Start hlog with the configured pipeline (background by default)
    run         Alias for start
    stop        Stop the hlog process
    restart     Restart hlog
    status      Show hlog status
    test        Validate hlog and hlquery configuration
    version     Show version information
    help        Show this help message

Options:

    --config FILE       Override hlog config file (default: $DEFAULT_HLOG_CONFIG)
    --pidfile FILE      Override pidfile (default: $DEFAULT_PIDFILE)
    --logfile FILE      Override log file (default: $DEFAULT_LOGFILE)
    --json              Emit machine-readable JSON from the wrapper
    --nofork            Run in foreground
    --quiet             Suppress startup banner
    --mode MODE         Override watcher mode
    --interval MS       Override poll interval
    --from-start        Read files from beginning
    --file PATH         Add explicit file input (repeatable)

Examples:

    $0 start
    $0 start --nofork
    $0 status
    $0 stop
    $0 start --config run/conf/hlog.conf
    $0 start --file /var/log/app.log --mode poll --interval 1000

EOF
}

sub print_info {
    my ($msg) = @_;
    return if $json_output;
    print "${BLACK}[${RESET} ${BLUE}INFO${RESET} ${BLACK}]${RESET} $msg\n";
}

sub print_success {
    my ($msg) = @_;
    return if $json_output;
    print "${BLACK}[${RESET} ${GREEN}OK${RESET} ${BLACK}]${RESET} $msg\n";
}

sub print_error {
    my ($msg) = @_;
    return if $json_output;
    print STDERR "${BLACK}[${RESET} ${RED}ERROR${RESET} ${BLACK}]${RESET} $msg\n";
}

sub print_warning {
    my ($msg) = @_;
    return if $json_output;
    print "${BLACK}[${RESET} ${YELLOW}WARN${RESET} ${BLACK}]${RESET} $msg\n";
}

sub json_escape {
    my ($value) = @_;
    return '' unless defined $value;
    $value = "$value";
    $value =~ s/\\/\\\\/g;
    $value =~ s/"/\\"/g;
    $value =~ s/\n/\\n/g;
    $value =~ s/\r/\\r/g;
    $value =~ s/\t/\\t/g;
    return $value;
}

sub json_value {
    my ($value) = @_;
    return 'null' unless defined $value;
    return $value if !ref($value) && $value =~ /^-?\d+(?:\.\d+)?$/;
    return 'true' if !ref($value) && $value eq '__JSON_TRUE__';
    return 'false' if !ref($value) && $value eq '__JSON_FALSE__';
    return '"' . json_escape($value) . '"';
}

sub emit_json {
    my (%data) = @_;
    my @pairs;
    foreach my $key (sort keys %data) {
        push @pairs, '"' . json_escape($key) . '":' . json_value($data{$key});
    }
    print '{' . join(',', @pairs) . "}\n";
}

sub strip_ansi {
    my ($text) = @_;
    return '' unless defined $text;
    $text =~ s/\e\[[0-9;]*[A-Za-z]//g;
    return $text;
}

sub startup_log_size {
    my ($logfile) = @_;
    return 0 unless -f $logfile;
    my @stat = stat($logfile);
    return $stat[7] || 0;
}

sub read_startup_delta {
    my ($logfile, $offset) = @_;
    return '' unless -f $logfile;

    open my $fh, '<', $logfile or return '';
    seek($fh, $offset, 0);
    local $/;
    my $data = <$fh>;
    close $fh;

    $data = strip_ansi($data // '');
    $data =~ s/\r//g;
    return $data;
}

sub parse_startup_output {
    my ($text) = @_;
    my @lines = grep { length($_) } split /\n/, ($text // '');
    my %startup = (
        mode => undef,
        interval_ms => undef,
        inputs => undef,
        hlog_config => undef,
        input_files => [],
        outputs => [],
    );

    foreach my $line (@lines) {
        $line =~ s/^\s+|\s+$//g;
        next unless length($line);

        if ($line =~ /\[pipeline\] mode=([a-z]+) interval_ms=(\d+) inputs=(\d+)\.$/) {
            $startup{mode} = $1;
            $startup{interval_ms} = int($2);
            $startup{inputs} = int($3);
            next;
        }

        if ($line =~ /\[pipeline\] hlog_config=(.+)\.$/) {
            $startup{hlog_config} = $1;
            next;
        }

        if ($line =~ /\[input_file\] path=(.+?) start_position=(beginning|end)\.$/) {
            push @{$startup{input_files}}, {
                path => $1,
                start_position => $2,
            };
            next;
        }

        if ($line =~ /\[output_hlquery\] endpoint=(.+?) collection=(.+)\.$/) {
            push @{$startup{outputs}}, {
                endpoint => $1,
                collection => $2,
            };
            next;
        }
    }

    return \%startup;
}

sub extract_startup_error {
    my ($text) = @_;
    my @lines = grep { length($_) } split /\n/, ($text // '');

    foreach my $line (@lines) {
        $line =~ s/^\s+|\s+$//g;
        next unless length($line);

        if ($line =~ /\[ERROR\]\s*(.+)$/) {
            return $1;
        }
    }

    return undef;
}

sub print_startup_summary {
    my ($startup, $logfile) = @_;
    my $timestamp = strftime("%b/%d - %H:%M:%S", localtime());

    print_success("Starting hlog: [$timestamp]");

    if (defined $startup->{inputs}) {
        print_success("Opened $startup->{inputs} log" . ($startup->{inputs} == 1 ? '' : 's') . ".");
    }

    if (defined $startup->{mode}) {
        my $message = "Using watcher mode: $startup->{mode}";
        $message .= " ($startup->{interval_ms} ms)" if defined $startup->{interval_ms};
        print_success($message . ".");
    }

    if (@{$startup->{input_files}}) {
        print_success("Watching files:");
        foreach my $input (@{$startup->{input_files}}) {
            print "       - $input->{path} (from $input->{start_position})\n";
        }
    }

    if (@{$startup->{outputs}}) {
        print_success("Writing to hlquery:");
        foreach my $output (@{$startup->{outputs}}) {
            print "       - $output->{endpoint} (collection: $output->{collection})\n";
        }
    }

    print_success("Now up and running.");
    print_success("Now detaching.");
    print_info("log file: $logfile");
}

sub find_binary {
    my $script_dir = dirname(abs_path($0));
    my @candidates = (
        File::Spec->catfile("${HLOG_BASE_DIR}", "build", "bin", $BINARY_NAME . $EXE_SUFFIX),
        File::Spec->catfile($script_dir, "bin", $BINARY_NAME . $EXE_SUFFIX),
        File::Spec->catfile($script_dir, "..", "build", "bin", $BINARY_NAME . $EXE_SUFFIX),
        $BINARY_NAME,
        $BINARY_NAME . $EXE_SUFFIX,
    );

    foreach my $candidate (@candidates) {
        return $candidate if -x $candidate;
    }

    return undef;
}

sub normalize_wrapper_args {
    my (@args) = @_;
    my @binary_args;
    my $nofork = 0;
    my $pidfile = $DEFAULT_PIDFILE;
    my $logfile = $DEFAULT_LOGFILE;

    while (@args) {
        my $arg = shift @args;
        if ($arg eq '--json') {
            $json_output = 1;
        } elsif ($arg eq '--nofork') {
            $nofork = 1;
            push @binary_args, '--nofork';
        } elsif ($arg eq '--config') {
            my $value = shift @args;
            die "--config requires a value\n" unless defined $value;
            push @binary_args, '--hlog-config', $value;
        } elsif ($arg eq '--pidfile') {
            my $value = shift @args;
            die "--pidfile requires a value\n" unless defined $value;
            $pidfile = $value;
        } elsif ($arg eq '--logfile') {
            my $value = shift @args;
            die "--logfile requires a value\n" unless defined $value;
            $logfile = $value;
        } else {
            push @binary_args, $arg;
        }
    }

    return (\@binary_args, $nofork, $pidfile, $logfile);
}

sub read_pid {
    my ($pidfile) = @_;
    return undef unless -f $pidfile;
    open my $fh, '<', $pidfile or return undef;
    my $pid = <$fh>;
    close $fh;
    chomp $pid if defined $pid;
    return ($pid && $pid =~ /^\d+$/) ? $pid : undef;
}

sub is_running {
    my ($pid) = @_;
    return 0 unless defined $pid;
    return 0 if $IS_WINDOWS;
    return kill 0, $pid;
}

sub resolve_start_args {
    my (@args) = @_;
    my ($binary_args, $nofork, $pidfile, $logfile) = normalize_wrapper_args(@args);
    my $binary = find_binary_or_die();

    my @exec_args = @$binary_args;
    if (!grep { $_ eq '--hlog-config' } @exec_args) {
        unshift @exec_args, $DEFAULT_HLOG_CONFIG;
        unshift @exec_args, '--hlog-config';
    }

    return ($binary, \@exec_args, $nofork, $pidfile, $logfile);
}

sub stop_process {
    my ($pidfile) = @_;
    if ($IS_WINDOWS) {
        return (-2, undef);
    }

    my $pid = read_pid($pidfile);

    if (!$pid || !is_running($pid)) {
        unlink $pidfile if -f $pidfile;
        return (0, undef);
    }

    kill 'TERM', $pid;
    for (1..10) {
        if (!is_running($pid)) {
            unlink $pidfile if -f $pidfile;
            return (1, $pid);
        }
        sleep 1;
    }

    kill 'KILL', $pid;
    sleep 2;

    if (!is_running($pid)) {
        unlink $pidfile if -f $pidfile;
        return (1, $pid);
    }

    return (-1, $pid);
}

sub find_binary_or_die {
    my $binary = find_binary();
    if (!$binary) {
        emit_json(action => 'binary', status => 'error', message => 'Could not find hlog binary') if $json_output;
        print_error("Could not find hlog binary");
        exit 1;
    }
    return $binary;
}

sub start_server {
    my (@args) = @_;
    my ($binary, $exec_args, $nofork, $pidfile, $logfile) = resolve_start_args(@args);
    my $startup_offset = startup_log_size($logfile);

    my $existing = read_pid($pidfile);
    if ($existing && is_running($existing)) {
        emit_json(action => 'start', status => 'running', pid => $existing, pidfile => $pidfile) if $json_output;
        print_warning("hlog is already running with pid $existing");
        exit 0;
    }
    unlink $pidfile if -f $pidfile;

    if ($nofork) {
        emit_json(action => 'start', status => 'foreground', pidfile => $pidfile, logfile => $logfile) if $json_output;
        print_success("Starting hlog in foreground");
        $ENV{HLOG_FOREGROUND} = '1';
        delete $ENV{HLOG_DAEMON};
        delete $ENV{HLOG_PIDFILE};
        exec $binary, @$exec_args or do {
            emit_json(action => 'start', status => 'error', message => "Failed to execute $binary: $!") if $json_output;
            print_error("Failed to execute $binary: $!");
            exit 1;
        };
    }

    if ($IS_WINDOWS) {
        emit_json(action => 'start', status => 'error', message => 'Background mode is not supported on Windows; use --nofork') if $json_output;
        print_error("Background mode is not supported on Windows; use --nofork");
        exit 1;
    }

    my $logdir = dirname($logfile);
    my $piddir = dirname($pidfile);
    mkdir $logdir unless -d $logdir;
    mkdir $piddir unless -d $piddir;

    my $pid = fork();
    if (!defined $pid) {
        emit_json(action => 'start', status => 'error', message => "fork() failed: $!") if $json_output;
        print_error("fork() failed: $!");
        exit 1;
    }

    if ($pid) {
        open my $pfh, '>', $pidfile or do {
            emit_json(action => 'start', status => 'error', message => "Failed to write pidfile $pidfile: $!") if $json_output;
            print_error("Failed to write pidfile $pidfile: $!");
            exit 1;
        };
        print {$pfh} $pid, "\n";
        close $pfh;
        my $startup_text = '';
        my $boot_ok = 0;
        my $boot_error;
        for (1..30) {
            select undef, undef, undef, 0.1;
            $startup_text = read_startup_delta($logfile, $startup_offset);
            $boot_error = extract_startup_error($startup_text);
            last if defined $boot_error;

            if ($startup_text =~ /\[pipeline\] mode=/) {
                $boot_ok = 1;
                last;
            }

            my $waited = waitpid($pid, WNOHANG);
            if ($waited == $pid) {
                $boot_error = extract_startup_error($startup_text) || "hlog exited during startup";
                last;
            }
        }

        if (!$boot_ok && is_running($pid)) {
            $boot_ok = 1;
        }

        if (!$boot_ok) {
            unlink $pidfile if -f $pidfile;
            emit_json(
                action => 'start',
                status => 'error',
                pid => $pid,
                pidfile => $pidfile,
                logfile => $logfile,
                message => $boot_error || 'hlog failed to start',
            ) if $json_output;
            print_error($boot_error || 'hlog failed to start');
            exit 1;
        }

        my $startup = parse_startup_output($startup_text);
        emit_json(
            action => 'start',
            status => 'started',
            pid => $pid,
            pidfile => $pidfile,
            logfile => $logfile,
            mode => $startup->{mode},
            inputs => $startup->{inputs},
        ) if $json_output;
        print_startup_summary($startup, $logfile);
        exit 0;
    }

    POSIX::setsid();
    $ENV{HLOG_DAEMON} = '1';
    $ENV{HLOG_PIDFILE} = $pidfile;
    delete $ENV{HLOG_FOREGROUND};
    open STDIN, '<', '/dev/null';
    open STDOUT, '>>', $logfile or die "Cannot open $logfile: $!";
    open STDERR, '>>', $logfile or die "Cannot open $logfile: $!";
    exec $binary, @$exec_args or die "Failed to execute $binary: $!";
}

sub stop_server {
    my (@args) = @_;
    my (undef, undef, $pidfile, undef) = normalize_wrapper_args(@args);
    if ($IS_WINDOWS) {
        emit_json(action => 'stop', status => 'error', pidfile => $pidfile, message => 'stop is not supported on Windows wrapper mode') if $json_output;
        print_error("stop is not supported on Windows wrapper mode");
        exit 1;
    }

    my $pid = read_pid($pidfile);
    if (!$pid || !is_running($pid)) {
        unlink $pidfile if -f $pidfile;
        emit_json(action => 'stop', status => 'stopped', pidfile => $pidfile, already_stopped => '__JSON_TRUE__') if $json_output;
        print_info("hlog is not running.");
        exit 0;
    }

    print_info("Stopping hlog (PID: $pid) ...");
    kill 'TERM', $pid;

    my $count = 0;
    while ($count < 10 && is_running($pid)) {
        if ($count == 3) {
            print_info("Still waiting for shutdown. Slow stops usually mean background writes, flushes, or open requests are finishing.");
        }
        sleep 1;
        $count++;
    }

    if (is_running($pid)) {
        print_warning("Graceful shutdown timed out, sending KILL signal...");
        kill 'KILL', $pid;
        sleep 2;
    }

    if (is_running($pid)) {
        emit_json(action => 'stop', status => 'error', pid => $pid, pidfile => $pidfile, message => 'Failed to stop hlog') if $json_output;
        print_error("Failed to stop hlog.");
        exit 1;
    }

    unlink $pidfile if -f $pidfile;
    emit_json(action => 'stop', status => 'stopped', pid => $pid, pidfile => $pidfile) if $json_output;
    print_success("hlog stopped successfully.");
    exit 0;
}

sub show_status {
    my (@args) = @_;
    my (undef, undef, $pidfile, undef) = normalize_wrapper_args(@args);
    if ($IS_WINDOWS) {
        emit_json(action => 'status', status => 'error', pidfile => $pidfile, message => 'status is not supported on Windows wrapper mode') if $json_output;
        print_error("status is not supported on Windows wrapper mode");
        exit 1;
    }
    my $pid = read_pid($pidfile);

    if ($pid && is_running($pid)) {
        emit_json(action => 'status', status => 'running', pid => $pid, pidfile => $pidfile) if $json_output;
        print_success("hlog is running (PID: $pid).");
        exit 0;
    }

    unlink $pidfile if -f $pidfile;
    emit_json(action => 'status', status => 'stopped', pidfile => $pidfile) if $json_output;
    print_info("hlog is not running.");
    exit 1;
}

sub restart_server {
    my (@args) = @_;
    my (undef, undef, $pidfile, undef) = normalize_wrapper_args(@args);
    my ($result, $pid) = stop_process($pidfile);

    if ($result == 1) {
        print_success("hlog stopped successfully.");
    } elsif ($result == -2) {
        emit_json(action => 'restart', status => 'error', pidfile => $pidfile, message => 'restart is not supported on Windows wrapper mode') if $json_output;
        print_error("restart is not supported on Windows wrapper mode");
        exit 1;
    } elsif ($result == -1) {
        emit_json(action => 'restart', status => 'error', pid => $pid, pidfile => $pidfile, message => 'Failed to stop hlog') if $json_output;
        print_error("Failed to stop hlog.");
        exit 1;
    } else {
        print_info("hlog is not running.");
    }

    start_server(@args);
}

my $command = shift @ARGV // 'help';

if ($command eq 'help' || $command eq '--help' || $command eq '-h') {
    my (undef) = normalize_wrapper_args(@ARGV);
    emit_json(action => 'help', status => 'ok', message => 'Use --help for usage text') if $json_output;
    print_usage();
    exit 0;
}

if ($command eq 'version') {
    my (undef) = normalize_wrapper_args(@ARGV);
    if ($json_output) {
        emit_json(action => 'version', status => 'ok', version => $VERSION);
        exit 0;
    }
    print "hlog $VERSION\n";
    exit 0;
}

if ($command eq 'start' || $command eq 'run') {
    start_server(@ARGV);
}

if ($command eq 'stop') {
    stop_server(@ARGV);
}

if ($command eq 'status') {
    show_status(@ARGV);
}

if ($command eq 'restart') {
    restart_server(@ARGV);
}

if ($command eq 'test') {
    my ($binary, $exec_args) = resolve_start_args(@ARGV);
    my @args = (@$exec_args, '--test-config');
    emit_json(action => 'test', status => 'starting') if $json_output;
    print_success("Testing hlog configuration");
    exec $binary, @args or do {
        emit_json(action => 'test', status => 'error', message => "Failed to execute $binary: $!") if $json_output;
        print_error("Failed to execute $binary: $!");
        exit 1;
    };
}

emit_json(action => $command, status => 'error', message => "Unknown command: $command") if $json_output;
print_error("Unknown command: $command");
print_usage();
exit 1;
