package make::configure::probes;

use v5.26.0;
use strict;
use warnings FATAL => qw(all);

use Exporter qw(import);
use File::Path qw(mkpath);
use File::Spec::Functions qw(catdir catfile);
use Text::ParseWords qw(shellwords);

use make::configure::files qw(write_file);

our @EXPORT_OK = qw(run_compile_probe);

sub run_compile_probe {
    my (%args) = @_;
    my $name = $args{name};
    my $source = $args{source};
    my $base_path = $args{base_path};
    my $root_path = $args{root_path};
    my $cxx = $args{cxx};
    my $cxxflags = $args{cxxflags} || '';
    my $ldflags = $args{ldflags} || '';

    my $probe_dir = catdir($base_path, 'build', 'configure-probes');
    mkpath($probe_dir) unless -d $probe_dir;

    my $src = catfile($probe_dir, "$name.cpp");
    my $bin = catfile($probe_dir, $name);
    write_file($src, $source);

    my @cmd = (
        $cxx,
        '-std=c++20',
        shellwords($cxxflags),
        '-I' . catdir($base_path, 'include'),
        '-I' . catdir($root_path, 'include'),
        '-I' . catdir($root_path, 'vendor'),
        '-I' . catdir($root_path, 'etc', 'api', 'cpp', 'vendor', 'json'),
        $src,
        '-o',
        $bin,
        shellwords($ldflags),
    );
    my $ok = (system(@cmd) == 0);
    unlink $src if -f $src;
    unlink $bin if -f $bin;
    return $ok;
}

1;
