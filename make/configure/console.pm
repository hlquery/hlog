package make::configure::console;

use v5.26.0;
use strict;
use warnings FATAL => qw(all);

use Exporter qw(import);

use constant {
    RESET       => "\033[0m",
    BOLD        => "\033[1m",
    BLACK       => "\033[30m",
    WHITE       => "\033[37m",
    BRIGHT_BLUE => "\033[94m",
    OKGREEN     => "\033[92m",
    OKCYAN      => "\033[96m",
    OKBLUE      => "\033[94m",
    WARNING     => "\033[1;38;5;208m",
    FAIL        => "\033[91m",
};

our @EXPORT_OK = qw(
    detect_terminal_support
    get_char_map
    print_error
    print_header
    print_section
    print_status
    print_step
    print_success
    print_warning
);

sub detect_terminal_support {
    return 0 unless -t STDOUT;
    my $term = $ENV{TERM} || '';
    return 0 if $term eq 'dumb';
    return 0 if $ENV{NO_COLOR};
    return 0 if $ENV{FORCE_COLOR} && $ENV{FORCE_COLOR} eq '0';
    return 0 if $ENV{CI} && !$ENV{FORCE_COLOR};
    return 1;
}

sub get_char_map {
    my ($ansi_supported) = @_;

    return (
        'checkmark_ok'   => $ansi_supported ? '✔' : '[OK]',
        'checkmark_fail' => $ansi_supported ? '✘' : '[FAIL]',
        'gear'           => $ansi_supported ? '•' : '-',
        'star'           => $ansi_supported ? '•' : '-',
        'arrow'          => $ansi_supported ? '⟶' : '>',
        'asterisk'       => $ansi_supported ? '•' : '*',
    );
}

sub print_header {
    my (%args) = @_;
    my $ansi_supported = $args{ansi_supported};
    my $project_name = $args{project_name} // 'Project';
    my $bold_start = $ansi_supported ? BOLD : "";
    my $white_start = $ansi_supported ? WHITE : "";
    my $reset_end = $ansi_supported ? RESET : "";
    print "\n" . $bold_start . $white_start . $project_name . " Configuration" . $reset_end . "\n";
}

sub print_status {
    my (%args) = @_;
    my $ansi_supported = $args{ansi_supported};
    my $char_map = $args{char_map} // {};
    my $status = $args{status};
    my $description = $args{description};
    my $value = $args{value};

    my $label = $status;
    $label = 'ERROR' if $status eq 'FAIL' || $status eq 'ERROR';

    my $status_color = "";
    if ($status eq 'OK') {
        $status_color = $ansi_supported ? OKGREEN : "";
    } elsif ($status eq 'SKIP') {
        $status_color = $ansi_supported ? WARNING : "";
    } elsif ($status eq 'AVAIL') {
        $status_color = $ansi_supported ? OKCYAN : "";
    } elsif ($status eq 'FAIL' || $status eq 'ERROR') {
        $status_color = $ansi_supported ? FAIL : "";
    } elsif ($status eq 'INFO') {
        $status_color = $ansi_supported ? OKBLUE : "";
    }

    my $tag_content = " " . $label . " ";
    my $lb = $ansi_supported ? BLACK . "[" . RESET : "[";
    my $rb = $ansi_supported ? BLACK . "]" . RESET : "]";
    my $bracket_status = $lb . $status_color . $tag_content . ($ansi_supported ? RESET : "") . $rb;

    my $tag_width = length($label) + 4;
    my $gap = 10 - $tag_width;
    $gap = 1 if $gap < 1;
    my $indent = " " x $gap;

    if (defined $value) {
        my $checkmark = "";
        if ($status eq 'OK' || $status eq 'AVAIL') {
            if ($value !~ /^(no|not available|not found|disabled)\b/i) {
                $checkmark = $ansi_supported
                    ? " " . OKGREEN . ($char_map->{checkmark_ok} // '[OK]') . RESET
                    : " " . ($char_map->{checkmark_ok} // '[OK]');
            }
        } elsif ($status eq 'SKIP' || $status eq 'FAIL' || $status eq 'ERROR') {
            if ($value =~ /^(no|not available|not found|disabled|failed)\b/i) {
                $checkmark = $ansi_supported
                    ? " " . FAIL . ($char_map->{checkmark_fail} // '[FAIL]') . RESET
                    : " " . ($char_map->{checkmark_fail} // '[FAIL]');
            }
        }

        my $bold_start = $ansi_supported ? BOLD : "";
        my $bold_end = $ansi_supported ? RESET : "";

        printf "%s%s%s .. %s%s%s%s\n",
            $bracket_status,
            $indent,
            $description,
            $bold_start,
            $value,
            $bold_end,
            $checkmark;
    } else {
        printf "%s%s%s\n", $bracket_status, $indent, $description;
    }
}

sub print_step {
    my (%args) = @_;
    print_status(%args, status => 'INFO');
}

sub print_success {
    my (%args) = @_;
    print_status(%args, status => 'OK');
}

sub print_warning {
    my (%args) = @_;
    print_status(%args, status => 'SKIP');
}

sub print_error {
    my (%args) = @_;
    print_status(%args, status => 'ERROR');
}

sub print_section {
    my (%args) = @_;
    my $ansi_supported = $args{ansi_supported};
    my $title = $args{title};
    print "\n";
    my $indent = "  ";
    my $total_width = 50;
    my $title_length = length($title);
    my $available_width = $total_width - length($indent) - $title_length - 2;
    my $rule_length = int($available_width / 2);
    $rule_length = 4 if $rule_length < 4;
    my $rule = '─' x $rule_length;
    my $bold_start = $ansi_supported ? BOLD : "";
    my $reset_end = $ansi_supported ? RESET : "";
    print $indent . $bold_start . $rule . " " . $title . " " . $rule . $reset_end . "\n\n";
}

1;
