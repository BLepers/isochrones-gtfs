#!/usr/bin/perl
use strict;
use warnings;
use v5.10;
use experimental 'smartmatch';
use Data::Dumper;
use Encode;
use JSON qw( decode_json encode_json );     # From CPAN

opendir(DIR, ".");
my @files = grep(/it-.*\.json$/,readdir(DIR));
closedir(DIR);

my $idx = 0;
foreach my $file (@files) {
   print "$file\n";
   my $json = decode_json(`cat "$file"`);
   my $url = $json->{urls}->{latest};
   system("wget -O latest.zip \"$url\"");
   system("unzip latest.zip");
   system("mkdir out$idx");
   system("mv *txt out$idx");
   $idx++;
}
