#/bin/bash

export MAPBOX_ACCESS_TOKEN=""
export mapbox_username=  # your mapbox username

city="Neuchâtel"
date=20221124 # Make sure it is a current date! E.g., if you read this file in 2025, at least a date in 2025!
gtfs_dir="./swiss"
tileset_id="swiss-neuch"

echo "Parsing GTFS...\n";
./parse "${gtfs_dir}" "${city}" "${date}" 0 > ${tileset_id}.json

echo "Getting the shape of the railways (may take a long time)...\n";
./query-osrm.pl ${tileset_id}.json > ${tileset_id}-rails.geojson

echo "Formatting station times...\n";
./output-stations.pl ${tileset_id}.json > ${tileset_id}-stations.geojson

tileset_recipe="
{
  \"version\": 1,
  \"layers\": {
    \"${tileset_id}-rails\": {
      \"source\": \"mapbox://tileset-source/${mapbox_username}/${tileset_id}-rails\",
      \"minzoom\": 1,
      \"maxzoom\": 10
    }
  }
}
"
echo $tileset_recipe > ${tileset_id}-rails.recipe

tilesets upload-source --replace ${mapbox_username} ${tileset_id}-rails ${tileset_id}-rails.geojson
tilesets create ${mapbox_username}.${tileset_id}-rails --recipe ${tileset_id}-rails.recipe --name "${tileset_id}-rails"
tilesets publish ${mapbox_username}.${tileset_id}-rails


tileset_recipe="
{
  \"version\": 1,
  \"layers\": {
    \"${tileset_id}-stations\": {
      \"source\": \"mapbox://tileset-source/${mapbox_username}/${tileset_id}-stations\",
      \"minzoom\": 1,
      \"maxzoom\": 10
   }
  }
}
"
echo $tileset_recipe > ${tileset_id}-stations.recipe

tilesets upload-source --replace ${mapbox_username} ${tileset_id}-stations ${tileset_id}-stations.geojson
tilesets create ${mapbox_username}.${tileset_id}-stations --recipe ${tileset_id}-stations.recipe --name "${tileset_id}-stations"
tilesets publish ${mapbox_username}.${tileset_id}-stations
