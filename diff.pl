#!/usr/bin/perl
use strict;
use warnings;
use Data::Dumper;
use JSON qw( decode_json encode_json );     # From CPAN

# Usage ./parse ... > log1; ./parse ... > log2; ./diff.pl log1 log2
# Log format:
# [ { dst:"Neuchâtel-Serrières", dsttrain:1, dstlat:46.984, dstlon:6.90369, src:"Neuchâtel", srctrain:1, srclat:46.997, srclon:6.93587, dur:2 },
#   { dst:"Les Deurres", dsttrain:1, dstlat:46.9853, dstlon:6.89877, src:"Neuchâtel", srctrain:1, srclat:46.9969, srclon:6.9359, dur:3 }

my $file1 = shift;
my $file2 = shift;
my %durations;

my $text1 = `cat "$file1"`;
$text1 =~ s/:/=>/smg; #Output of the parser is for JS, but easy to transform to perl code
my $stops1 = eval($text1);
foreach my $stop (@$stops1) {
   $durations{$stop->{dst}} //= $stop->{dur};
}

my $text2 = `cat "$file2"`;
$text2 =~ s/:/=>/smg; 
my $stops2 = eval($text2);
foreach my $stop (@$stops2) {
   $stop->{dur} -= ($durations{$stop->{dst}} // 0);
}

print JSON->new->utf8(0)->encode($stops2);
