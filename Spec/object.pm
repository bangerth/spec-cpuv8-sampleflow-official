# TODO
# TODO: This is an incomplete template; you will need to fill in @sources and the invoke() function (at least)
# TODO
$benchnum  = '747';
$benchname = 'sampleflow_r';
$exename   = 'sampleflow_r';
$benchlang = '?'; # TODO: Fill this in: "C", "CXX,C", etc.
@base_exe  = ($exename);

$calctol = 0;

use Config;
$bench_flags  = '-DSPEC_AUTO_BYTEORDER=0x'.$Config{'byteorder'};
$bench_flags .= ' -DSPEC_AUTO_SUPPRESS_THREADING'; # TODO: fix this up with your actual set of flags

@sources = (qw(
    source_file.c ... TODO: fix this up with your actual set of source files
    ));

# Return a list of hashes describing runs to do.
sub invoke {
    my ($me) = @_;
    my (@rc);

    return @rc;
}


1;

# Editor settings: (please leave this at the end of the file)
# vim: set filetype=perl syntax=perl shiftwidth=4 tabstop=8 expandtab nosmarttab:
