package make::configure::system;

use v5.26.0;
use strict;
use warnings FATAL => qw(all);

use Config qw(%Config);
use Exporter qw(import);
use File::Spec::Functions qw(catfile);

our @EXPORT_OK = qw(
    command_exists
    default_cxx
    get_compiler_version
    get_cpu_count
    get_make_command
    get_system_name
    get_version_from_script
    is_bsd_make_platform
    platform_module_suffix
);

sub get_system_name {
    my $system = $Config{osname} || $^O || 'unknown';
    return 'Windows' if $^O eq 'MSWin32';
    return 'Darwin' if $system =~ /darwin/i;
    return 'FreeBSD' if $system =~ /freebsd/i;
    return 'OpenBSD' if $system =~ /openbsd/i;
    return 'NetBSD' if $system =~ /netbsd/i;
    return 'DragonFly' if $system =~ /dragonfly/i;
    return 'Linux' if $system =~ /linux/i;
    return $system;
}

sub is_bsd_make_platform {
    my $system = get_system_name();
    return scalar grep { $_ eq $system } qw(Darwin FreeBSD OpenBSD NetBSD DragonFly);
}

sub get_make_command {
    return 'make' if $^O eq 'MSWin32';
    return is_bsd_make_platform() ? 'gmake' : 'make';
}

sub get_cpu_count {
    my $count = 0;
    if ($^O eq 'MSWin32') {
        $count = $ENV{NUMBER_OF_PROCESSORS} || 0;
    } elsif (is_bsd_make_platform()) {
        $count = `sysctl -n hw.ncpu 2>/dev/null`;
    } else {
        $count = `nproc 2>/dev/null`;
    }
    chomp $count if defined $count;
    return ($count && $count =~ /^\d+$/ && $count > 0) ? $count : 1;
}

sub default_cxx {
    return $ENV{CXX} if $ENV{CXX};
    return 'c++';
}

sub command_exists {
    my ($cmd) = @_;
    return 0 unless defined $cmd && length $cmd;
    return 1 if $cmd =~ m{[\\/]} && -x $cmd;

    my $path_sep = $Config{path_sep} || ($^O eq 'MSWin32' ? ';' : ':');
    my @exts = ('');
    if ($^O eq 'MSWin32') {
        my $pathext = $ENV{PATHEXT} || '.COM;.EXE;.BAT;.CMD';
        @exts = grep { length $_ } split /;/, $pathext;
        unshift @exts, '' unless $cmd =~ /\.[A-Za-z0-9]+$/;
    }

    foreach my $dir (split /\Q$path_sep\E/, ($ENV{PATH} // '')) {
        next unless length $dir;
        foreach my $ext (@exts) {
            my $candidate = catfile($dir, $cmd . $ext);
            return 1 if -f $candidate && -x $candidate;
        }
    }

    return 0;
}

sub get_compiler_version {
    my ($cxx) = @_;
    my $version = `$cxx --version 2>/dev/null`;
    chomp $version;
    my ($first) = split /\n/, $version;
    return $first || 'unknown';
}

sub get_version_from_script {
    my (%args) = @_;
    my $base_path = $args{base_path};
    my $root_path = $args{root_path};

    for my $script (
        catfile($base_path, 'src', 'version.sh'),
        catfile($root_path, 'src', 'version.sh')
    ) {
        next unless -f $script;

        open my $fh, '<', $script or next;
        local $/;
        my $content = <$fh>;
        close $fh;

        if ($content =~ /echo\s+"(?:hlquery|hlog)-([^"\r\n]+)"/) {
            return $1;
        }
    }

    return '1.0.0';
}

sub platform_module_suffix {
    return '.dll' if $^O eq 'MSWin32';
    return '.dylib' if get_system_name() eq 'Darwin';
    return '.so';
}

1;
