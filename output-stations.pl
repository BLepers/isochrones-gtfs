#!/usr/bin/perl
use strict;
use warnings;
use v5.10;
use LWP::Simple;                # From CPAN
use JSON qw( decode_json encode_json );     # From CPAN
use Data::Dumper;               # Perl core module

if(!$ARGV[0]) {
	die "./query-osrm.pl <json output of parser>\n";
}

# Open parser output
my $text = `cat "$ARGV[0]"`;
$text =~ s/:/=>/smg; #Output of the parser is for JS, but easy to transform to perl code
my $stops = eval($text);


say '{ "features": [';

# Foreach stop
my $is_first = 1;
my %seen;
foreach my $stop (@$stops) { 
	#{ dst:"Nîmes", dstlat:43.8324, dstlon:4.36617, src:"St-Geniès-de-Malgoirès", srclat:43.9502, srclon:4.21494, dur:872 },
	my $k = "$stop->{dstlon}.$stop->{dstlat}";
	next if($seen{$k});
	$seen{$k} = 1;

	if(!$is_first) {
		print ",\n";
	} else {
		$is_first = 0;
	}
	printf ('{ "type": "Feature", "properties": { "name": "'.$stop->{dst}.'", "dur": '.$stop->{dur}.' }, "geometry": { "coordinates": [%f,%f], "type": "Point" } } ', $stop->{dstlon}, $stop->{dstlat});
}
say '], "type": "FeatureCollection" }';
