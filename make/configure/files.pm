package make::configure::files;

use v5.26.0;
use strict;
use warnings FATAL => qw(all);

use Exporter qw(import);
use File::Path qw(mkpath);
use File::Spec::Functions qw(catdir catfile);

our @EXPORT_OK = qw(
    check_runtime_writable
    create_required_directories
    generate_from_template
    generate_hlog_wrapper
    generate_version_script
    slurp_file
    write_file
);

sub slurp_file {
    my ($path) = @_;
    open my $fh, '<', $path or die "Cannot read $path: $!\n";
    local $/;
    my $content = <$fh>;
    close $fh;
    return $content;
}

sub write_file {
    my ($path, $content) = @_;
    open my $fh, '>', $path or die "Cannot write $path: $!\n";
    print {$fh} $content;
    close $fh;
}

sub generate_version_script {
    my (%args) = @_;
    my $base_path = $args{base_path};
    my $version = $args{version};

    my $version_path = catfile($base_path, 'src', 'version.sh');
    my $version_content = <<"EOF";
#!/bin/sh
echo "hlog-$version"
EOF

    write_file($version_path, $version_content);
    chmod 0755, $version_path;
}

sub create_required_directories {
    my (%args) = @_;
    my $base_path = $args{base_path};

    my @dirs = (
        catdir($base_path, 'build'),
        catdir($base_path, 'run'),
        catdir($base_path, 'run', 'bin'),
        catdir($base_path, 'run', 'conf'),
        catdir($base_path, 'run', 'data'),
        catdir($base_path, 'run', 'logs'),
        catdir($base_path, 'run', 'pid'),
    );

    my $created = 0;
    foreach my $dir (@dirs) {
        next if -d $dir;
        mkpath($dir);
        $created++;
    }

    return $created;
}

sub generate_from_template {
    my (%args) = @_;
    my $template_path = $args{template_path};
    my $output_path = $args{output_path};
    my $vars = $args{vars} // {};
    my $preserve_existing = $args{preserve_existing} // 0;

    return 0 if $preserve_existing && -e $output_path;

    my $content = slurp_file($template_path);
    for my $key (keys %{$vars}) {
        my $value = $vars->{$key};
        $content =~ s/\$\{$key\}/$value/g;
    }

    write_file($output_path, $content);
    return 1;
}

sub generate_hlog_wrapper {
    my (%args) = @_;
    my $base_path = $args{base_path};
    my $vars = $args{vars} // {};
    my $run_dir = $args{run_dir} // 'run';

    my $template_path = catfile($base_path, 'make', 'hlog.tpl');
    my $output_path = catfile($base_path, $run_dir, 'hlog');

    return 0 unless -f $template_path;

    my %wrapper_vars = (
        %{$vars},
        HLOG_BASE_DIR => $base_path,
    );

    generate_from_template(
        template_path => $template_path,
        output_path => $output_path,
        vars => \%wrapper_vars,
    );
    chmod 0755, $output_path;
    return 1;
}

sub check_runtime_writable {
    my ($dir) = @_;
    mkpath($dir) unless -d $dir;
    my $test = catfile($dir, '.configure-write-test');
    if (open my $fh, '>', $test) {
        print {$fh} "ok\n";
        close $fh;
        unlink $test;
        return 1;
    }
    return 0;
}

1;
