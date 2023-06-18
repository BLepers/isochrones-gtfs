#!/usr/bin/perl
use strict;
use warnings;
use v5.10;
use LWP::Simple;                # From CPAN
use JSON qw( decode_json encode_json );     # From CPAN
use Data::Dumper;               # Perl core module

my $routeosmr = "http://localhost:5000/route/v1/transit/%f,%f;%f,%f?overview=full&geometries=geojson";
my $matchosmr = "http://localhost:5000/match/v1/transit/%f,%f;%f,%f?overview=full&geometries=geojson&timestamps=10;1000&radiuses=30;30";

if(!$ARGV[0]) {
	die "./query-osrm.pl <json output of parser>\n";
}

# Open parser output
my $text = `cat "$ARGV[0]"`;
$text =~ s/:/=>/smg; #Output of the parser is for JS, but easy to transform to perl code
my $stops = eval($text);


#print "{ 'type': 'geojson', 'data': { 'type': 'FeatureCollection', 'features': [ \n";
say '{ "features": [';

sub get_json {
	my $txt = get($_[0]);
	if(!$txt || ($txt =~ /NoRoute/) || ($txt =~ /NoMatch/)) {
		return {};
	} else {
		return decode_json($txt);
	}
}

# Foreach stop
my $is_first = 1;
my ($done, $index, $prev_percent, $total) = (0, 0, 0, scalar(@$stops));
foreach my $stop (@$stops) {
	#{ dst:"Nîmes", dstlat:43.8324, dstlon:4.36617, src:"St-Geniès-de-Malgoirès", srclat:43.9502, srclon:4.21494, dur:872 },
	my ($url,$json,$dist,$best_dist,$best_coords);

   # Only color railways
   goto SKIP if(!$stop->{dsttrain} || !$stop->{srctrain});
       
	# Get the path
	$url = sprintf($routeosmr, $stop->{srclon}, $stop->{srclat}, $stop->{dstlon}, $stop->{dstlat});
	$json = get_json($url);
	$best_dist = $json->{routes}->[0]->{distance} // 0;
	$best_coords = $json->{routes}->[0]->{geometry}->{coordinates};

	# Favor matchings if they are much shorter to avoid silly routings, but prefer routings otherwise.
	$url = sprintf($matchosmr, $stop->{srclon}, $stop->{srclat}, $stop->{dstlon}, $stop->{dstlat});
	$json = get_json($url);
	$dist = $json->{matchings}->[0]->{distance} // 0;
	if($dist && (!$best_dist || $dist*2 < $best_dist)) {
		$best_dist = $dist;
		$best_coords = $json->{matchings}->[0]->{geometry}->{coordinates};
	} 

	# And try a few sampling routes because sometimes the match & route urls cannot find anything...
	for my $lon (-0.00100,0.00100,-0.00200,0.00200) {
		for my $lat (-0.00100,0.00100,-0.00200,0.00200) {
			$url = sprintf($routeosmr, $stop->{srclon} + $lon, $stop->{srclat} + $lat, $stop->{dstlon}, $stop->{dstlat});
			$json = get_json($url);
			$dist = $json->{routes}->[0]->{distance} // 0;
			if($dist && (!$best_dist || $dist*2 < $best_dist)) {
				$best_dist = $dist;
				$best_coords = $json->{routes}->[0]->{geometry}->{coordinates};
			}

			$url = sprintf($routeosmr, $stop->{srclon}, $stop->{srclat}, $stop->{dstlon} + $lon, $stop->{dstlat} + $lat);
			$json = get_json($url);
			$dist = $json->{routes}->[0]->{distance} // 0;
			if($dist && (!$best_dist || $dist*2 < $best_dist)) {
				$best_dist = $dist;
				$best_coords = $json->{routes}->[0]->{geometry}->{coordinates};
			}
		}
	}
	
	goto SKIP if(!$best_coords);
	if(!$is_first) {
		print ",\n";
	} else {
		$is_first = 0;
	}
	print '{ "type": "Feature", "properties": { "dst": "'.$stop->{dst}.'", "src": "'.$stop->{src}.'", "dur": '.$stop->{dur}.' }, "geometry": { "coordinates": '.encode_json($best_coords).', "type": "LineString" } } ';


SKIP:
   $index++;
   if(int($index*100/$total) != $prev_percent) {
      $prev_percent = int($index*100/$total);
      print STDERR "Finding railways geometry ($prev_percent%)\n";
   }
}
say '], "type": "FeatureCollection" }';
