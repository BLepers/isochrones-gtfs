#include <iterator>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <list>
#include "cvs.h"
#include "deg.h"

using namespace std;
using namespace io;

string _ = "";

void sanitize(string *s) {
  // Remove all double-quote characters
  s->erase(
    remove( s->begin(), s->end(), '\"' ),
    s->end()
    );
}

/* stops.txt => Stop objects */
void print_stops(char *dir) {
   string stop_file_name = _ + dir + "/stops.txt";
   io::CSVReader<4, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(stop_file_name);
   in.read_header(io::ignore_extra_column, "stop_id", "stop_name", "stop_lat", "stop_lon");

   string stop_id, stop_name;
   double stop_lat, stop_lon;
   while(in.read_row(stop_id, stop_name, stop_lat, stop_lon)) {
      sanitize(&stop_name);
      cout << "\"" << dir << "-" << stop_id << "\",\"" << stop_name << "\",\"" << stop_lat << "\",\"" << stop_lon << "\"\n";
   }
}

/* routes.txt => Route objects */
void print_routes(char *dir) {
   //route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
   string route_file_name = _ + dir + "/routes.txt";
   io::CSVReader<2, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(route_file_name);
   in.read_header(io::ignore_extra_column, "route_id", "route_type");

   int route_type;
   string route_id;
   while(in.read_row(route_id, route_type)) {
      cout << "\"" << dir << "-" << route_id << "\",\"" << route_type << "\"\n";
   }
}

/* trips.txt => Trip objects */
void print_trips(char *dir) {
   //route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
   string trip_file_name = _ + dir + "/trips.txt";
   io::CSVReader<3, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(trip_file_name);
   in.read_header(io::ignore_extra_column, "route_id", "service_id", "trip_id");

   string route_id, service_id, trip_id;
   while(in.read_row(route_id, service_id, trip_id)) {
      cout << "\"" << dir << "-" << route_id << "\",\"" << dir << "-" << service_id << "\",\"" << dir << "-" << trip_id << "\"\n";
   }
}

void print_calendar(char *dir) {
   //service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
   string trip_file_name = _ + dir + "/calendar.txt";
   ifstream trip_file(trip_file_name.c_str());
   if(!trip_file.good()) 
      return;

   io::CSVReader<10, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(trip_file_name);
   in.read_header(io::ignore_extra_column, "service_id", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday", "start_date", "end_date");

   string service_id, monday, tuesday, wednesday, thursday, friday, saturday, sunday, start_date, end_date;
   while(in.read_row(service_id, monday, tuesday, wednesday, thursday, friday, saturday, sunday, start_date, end_date)) {
      cout << "\"" <<  dir << "-" << service_id << "\",\"" << monday << "\",\"" << tuesday << "\",\"" << wednesday << "\",\"" << thursday << "\",\"" << friday << "\",\"" << saturday << "\",\"" << sunday << "\",\"" << start_date << "\",\"" << end_date << "\"\n";
   }
}

void print_calendar_dates(char *dir) {
   string trip_file_name = _ + dir + "/calendar_dates.txt";
   io::CSVReader<3, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(trip_file_name);
   in.read_header(io::ignore_extra_column, "service_id", "date", "exception_type");

   string service_id, date, exception_type;
   while(in.read_row(service_id, date, exception_type)) {
      cout << "\"" << dir << "-" << service_id << "\",\"" << date << "\",\"" << exception_type << "\"\n";
   }
}

/* Build the graph of all trajectories */
void print_trajectories(char *dir) {
   string trip_file_name = _ + dir + "/stop_times.txt";
   io::CSVReader<5, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(trip_file_name);
   in.read_header(io::ignore_extra_column, "trip_id", "arrival_time", "departure_time", "stop_id","stop_sequence");

   string trip_id, arrival_time, departure_time, stop_id;
   size_t stop_sequence = 0;

   while(in.read_row(trip_id, arrival_time, departure_time, stop_id, stop_sequence)) {
      cout << "\"" << dir << "-" << trip_id << "\",\"" << arrival_time << "\",\"" << departure_time << "\",\"" << dir << "-" << stop_id << "\",\"" << stop_sequence << "\"\n";
   }
}

void print_transfers(char *dir) {
   string trip_file_name = _ + dir + "/transfers.txt";
   ifstream trip_file(trip_file_name.c_str());
   if(!trip_file.good())
      return; // transfers file does not always exist

   io::CSVReader<3, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(trip_file_name);
   in.read_header(io::ignore_extra_column, "from_stop_id", "to_stop_id", "min_transfer_time");

   string from_stop_id, to_stop_id;
   int min_transfer_time;

   while(in.read_row(from_stop_id, to_stop_id, min_transfer_time)) {
      cout << "\"" << dir << "-" << from_stop_id << "\",\"" << dir << "-" << to_stop_id << "\",\"" << min_transfer_time << "\"\n";
   }
}

void redirect_cout(string out) {
   freopen(out.c_str(),"w",stdout);
}

int main(int argc, char **argv) {
   if(argc < 3) {
      cout << "Usage: merge <gtfs directories> <out directory>\n";
      return -1;
   }

   char *dir = argv[argc - 1];
   cout << "Merging to directory " << dir << "\n";
   for(size_t i = 1; i < argc - 1; i++)
      cout << "\t" << argv[i] << "\n";

   redirect_cout(_ + dir + "/stops.txt" );
   cout << "stop_id,stop_name,stop_lat,stop_lon\n";
   for(size_t i = 1; i < argc - 1; i++)
      print_stops(argv[i]);

   redirect_cout(_ + dir + "/routes.txt" );
   cout << "route_id,route_type\n";
   for(size_t i = 1; i < argc - 1; i++)
      print_routes(argv[i]);

   redirect_cout(_ + dir + "/trips.txt" );
   cout << "route_id,service_id,trip_id\n";
   for(size_t i = 1; i < argc - 1; i++)
      print_trips(argv[i]);

   redirect_cout(_ + dir + "/calendar.txt" );
   cout << "service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date\n";
   for(size_t i = 1; i < argc - 1; i++)
      print_calendar(argv[i]);

   redirect_cout(_ + dir + "/calendar_dates.txt" );
   cout << "service_id,date,exception_type\n";
   for(size_t i = 1; i < argc - 1; i++)
      print_calendar_dates(argv[i]);

   redirect_cout(_ + dir + "/stop_times.txt" );
   cout << "trip_id,arrival_time,departure_time,stop_id,stop_sequence\n";
   for(size_t i = 1; i < argc - 1; i++)
      print_trajectories(argv[i]);

   redirect_cout(_ + dir + "/transfers.txt" );
   cout << "from_stop_id,to_stop_id,min_transfer_time\n";
   for(size_t i = 1; i < argc - 1; i++)
      print_transfers(argv[i]);

   return 0;
}
