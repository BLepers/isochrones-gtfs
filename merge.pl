#!/usr/bin/perl
use strict;
use warnings;
use v5.10;
use experimental 'smartmatch';
use Data::Dumper;
use Encode;
use JSON qw( decode_json encode_json );     # From CPAN

# Merge outputs of the parser

my @files = @ARGV;
my @excluded_dst;
my %best;

for my $f (@files) {
   if($f =~ /\.(.*?)\.json$/) {
      push(@excluded_dst, $1);
   }
}

for my $f (@files) {
   my $text = `cat "$f"`;
   $text =~ s/:/=>/smg; #Output of the parser is for JS, but easy to transform to perl code
   my $stops = eval($text);
   foreach my $stop (@$stops) {
      next if($stop->{dst} ~~ @excluded_dst);

      my $old = $best{$stop->{dst}};
      if(!$old || $stop->{dur} < $old->{dur}) {
         $best{$stop->{dst}} = $stop;
      }
   }
}

my @values = values %best;
print JSON->new->utf8(0)->encode(\@values);
