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
string chosen_day = ""; // YYYYmmdd
int trains_only = 0; // Only parse trains
const int simplify_results = 1; // if true remove bus stops less than 2km away from train station in the output
const int exclude_frequently_cancelled_routes = 0; // exclude all routes cancelled more than 50\% of the time
const int max_travel_time = 600; // 10h

/* Previous stop in a trajectory, and used edge in the graph */
struct Source;
struct edge;

/* Stop (train station, bus stop, ...) */
struct Stop {
   int id;           // our internal id, not stop_id
   int active, next; // active in current iteration of sssp, in next iteration
   string stop_name; // E.g. "Lausanne"
   string stop_id;
   double stop_lat,stop_lon; // Latitude, Longitude

   int is_train;
   int is_close_to_train;

   int nb_hops;      // number of stops crossed to get there (stops, not train changes)
   int best_time;    // in minutes
   Source *best_source;
   list<Source*> parents; // different ways and times to get there; we chose the best one for every given bus/train leaving from us
                          // the best depends on travel time up until here, and arrival time
};
int nb_stops = 0;
std::map<string,Stop*> stops;       // stop_id (GTFS) => Stop
std::map<string,std::list<Stop*>*> stop_names;  // stop_name (GTFS) => Stop
std::map<int,Stop*> stop_ids;       // our internale id => Stop
std::list<Stop*> stops_sorted_by_lat;

/* Different ways to get to a Stop */
struct Source {
   Stop *parent;
   Stop *child;
   int travel_time;  // total travel time from the source, not just 1 hop
   int departure_time; // we departed from parent at departure_time (minutes)
   int arrival_time; // we arrive at arrival_time (minutes)
   int walking;      // proper connection or walking?
   Source *best;     // best possible route to here
   struct edge *edge;// we used that edge
};

/* Route.txt */
struct Route {
   string id;
   int is_train;
};
std::map<string,Route*> routes;       // route_id => Route

/* Trips.txt */
struct Trip {
   string id;
   int is_train;
   string service_id;
};
std::map<string,Trip*> trips;       // trip_id => Route

/* Calendar.txt & calendar_dates.txt  */
struct Calendar {
   int nb_expected;
   int nb_cancellations;
   int scheduled_on_chosen_date; // is it scheduled on the chosen day?
   int cancelled_on_chosen_date; // is it exceptionally cancelled on chosen day?
};
static int no_calendar_file = 0;
std::map<string,Calendar*> calendar; // trip_id => calendar


/* Graph representation for shortest path computation */
struct edge {
   int dst;
   int travel_time;
   int departure_time;
   string trip_id;
};
struct vertex {
   int nb_edges;
   std::vector<edge> edges;
};
struct vertex *vertices;


/* Helper function to execute a command and get its output as a string */
std::string exec(string cmd) {
   std::array<char, 128> buffer;
   std::string result;
   std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
   if (!pipe) {
      throw std::runtime_error("popen() failed!");
   }
   while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
      result += buffer.data();
   }
   return result;
}

/* stops.txt => Stop objects */
void create_stops(char *dir, char *origin) {
   string stop_file_name = _ + dir + "/stops.txt";
   io::CSVReader<4, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(stop_file_name);
   in.read_header(io::ignore_extra_column, "stop_id", "stop_name", "stop_lat", "stop_lon");

   string stop_id, stop_name;
   double stop_lat, stop_lon;
   while(in.read_row(stop_id, stop_name, stop_lat, stop_lon)) {
      Stop *s = new Stop();
      s = new Stop();
      s->stop_name = stop_name;
      s->id = nb_stops;
      s->stop_lat = stop_lat;
      s->stop_lon = stop_lon;
      s->active = 0;
      s->next = 0;
      s->nb_hops = -1;
      s->best_time = -1;
      s->is_train = 0;
      s->stop_id = stop_id;
      stop_ids[s->id] = s;
      if(!stop_names[stop_name]) 
         stop_names[stop_name] = new std::list<Stop*>();
      stop_names[stop_name]->push_back(s);
      nb_stops++;
      stops[stop_id] = s;
   }

   if(!stop_names[origin]) {
      cerr << "Origin stop '" << origin << "' doesn't exist\n";
      exit(-1);
   }
   for(auto s1: *stop_names[origin]) {
      for(auto s2: *stop_names[origin]) {
         double dst = distanceEarth(s1->stop_lat, s1->stop_lon, s2->stop_lat, s2->stop_lon);
         if(dst > 1000) { // Start stop is twice in the dataset at very different places!
            cerr << "Origin stop '" << origin << "' is present at very different locations, edit stops.txt\n";
            cerr << "\t Stop found at " << s1->stop_lat << "," << s1->stop_lon << "\n";
            cerr << "\t And           " << s2->stop_lat << "," << s2->stop_lon << "\n";
            cerr << "\t Which are " << (dst/1000.0) << "km away!\n";
            exit(-1);
         }
      }
   }
}

/* routes.txt => Route objects */
void create_routes(char *dir) {
   //route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
   string route_file_name = _ + dir + "/routes.txt";
   io::CSVReader<2, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(route_file_name);
   in.read_header(io::ignore_extra_column, "route_id", "route_type");

   int route_type;
   string route_id;
   while(in.read_row(route_id, route_type)) {
      Route *r = new Route();
      r->id = route_id;
      r->is_train = (route_type == 2) || (route_type >= 100 && route_type < 200);
      routes[r->id] = r;
   }
}

/* trips.txt => Trip objects */
void create_trips(char *dir) {
   //route_id,agency_id,route_short_name,route_long_name,route_desc,route_type
   string trip_file_name = _ + dir + "/trips.txt";
   io::CSVReader<3, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(trip_file_name);
   in.read_header(io::ignore_extra_column, "route_id", "service_id", "trip_id");

   string route_id, service_id, trip_id;
   while(in.read_row(route_id, service_id, trip_id)) {
      Trip *t = new Trip();
      t->id = trip_id;
      t->service_id = service_id;
      Route *r = routes[route_id];
      t->is_train = r?r->is_train:0;
      trips[t->id] = t;
   }
}

int nb_days_between(string start, string end) {
   tm tm_start{}, tm_end{};

   stringstream ss_start(start);
   ss_start >> get_time(&tm_start, "%Y%m%d");

   stringstream ss_end(end);
   ss_end >> get_time(&tm_end, "%Y%m%d");

   double diff = difftime(mktime(&tm_end), mktime(&tm_start))/60/60/24;
   return diff;
}

void create_expected_trips(char *dir) {
   //service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date
   string trip_file_name = _ + dir + "/calendar.txt";
   ifstream trip_file(trip_file_name.c_str());
   if(!trip_file.good()) {
      no_calendar_file = 1;
      return; // calendar file does not always exist
   }

   /* What is the weekday of the chosen date?*/
   int day;
   struct tm tm;
   memset(&tm, 0, sizeof(tm));
   if (strptime(chosen_day.c_str(), "%Y%m%d", &tm) != NULL) {
      time_t t = mktime(&tm);
      day = localtime(&t)->tm_wday;
      cerr << "Checking trips that happen on the " << chosen_day << " (which is a " << day << " -- 0 = Sunday, 1 = Monday, etc.)\n";
   } else {
      cerr << "Chosen date " << chosen_day << " is not a valid date.\n";
      exit(-1);
   }

   io::CSVReader<10, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(trip_file_name);
   in.read_header(io::ignore_extra_column, "service_id", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday", "start_date", "end_date");

   int trip_uid = 0;
   string service_id, monday, tuesday, wednesday, thursday, friday, saturday, sunday, start_date, end_date,  one = "1";
   while(in.read_row(service_id, monday, tuesday, wednesday, thursday, friday, saturday, sunday, start_date, end_date)) {
      Calendar *i = calendar[service_id];
      if(!i) {
         i = new Calendar();
         i->nb_expected = 0;
         i->nb_cancellations = 0;
         switch(day) {
            case 0:
               i->scheduled_on_chosen_date = (sunday == one);
               break;
            case 1:
               i->scheduled_on_chosen_date = (monday == one);
               break;
            case 2:
               i->scheduled_on_chosen_date = (tuesday == one);
               break;
            case 3:
               i->scheduled_on_chosen_date = (wednesday == one);
               break;
            case 4:
               i->scheduled_on_chosen_date = (thursday == one);
               break;
            case 5:
               i->scheduled_on_chosen_date = (friday == one);
               break;
            case 6:
               i->scheduled_on_chosen_date = (saturday == one);
               break;
            default:
               cerr << "Day > 6??\n";
               exit(-1);
         }
         i->cancelled_on_chosen_date = 0;
         calendar[service_id] = i;
      }

      int nb_days_per_week = (monday == one) + (tuesday == one) + (wednesday == one) + (thursday == one) + (friday == one) + (saturday == one) + (sunday == one);
      int nb_valid_days = nb_days_between(start_date, end_date);
      i->nb_expected = nb_days_per_week * (nb_valid_days/7);
   }
}

void create_cancelled_trips(char *dir) {
   string trip_file_name = _ + dir + "/calendar_dates.txt";
   io::CSVReader<3, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(trip_file_name);
   in.read_header(io::ignore_extra_column, "service_id", "date", "exception_type");

   int trip_uid = 0;
   string service_id, date, exception_type, one = "1", two = "2";
   size_t nb_exceptions = stol(exec(_ + "wc -l \"" + trip_file_name + "\"")), done = 0;
   while(in.read_row(service_id, date, exception_type)) {
      Calendar *i = calendar[service_id];
      if(!i) {
         if(!no_calendar_file)
            cerr << "Service " << service_id << " doesn't seem to have any valid date\n";
         i = new Calendar();
         i->nb_expected = 0;
         i->nb_cancellations = 0;
         i->scheduled_on_chosen_date = 0;
         i->cancelled_on_chosen_date = 0;
         calendar[service_id] = i;
      }
      if(date == chosen_day) {
         // Some GTFS datasets have a calendar.txt (expected schedule) with exceptions...
         if(exception_type == two)
            i->cancelled_on_chosen_date = 1;
         // Some GTFS datasets don't have a calandar.txt and all running trains are listed as exeptions...
         else if(exception_type == one)
            i->scheduled_on_chosen_date = 1;
      }
      if(exception_type == two) // 2 == cancelled
         i->nb_cancellations++;
      done++;
      if(done % 50000 == 0)
         cerr << "Adding exceptions " << done << "/" << nb_exceptions << " (" << (done*100/nb_exceptions) << "%)\n";
   }


   //for(auto i: calendar) {
   //cout << "Calendar " << i.first << " = [ " << i.second->nb_expected << ", " << i.second->nb_cancellations << " ]\n";
   //}
}


int char_to_int(char v) {
   return v - '0';
}
// 05:15:00 -> int (in minutes)
int string_to_time(string time) {
   return (char_to_int(time[0])*10 + char_to_int(time[1])) * 60
      + (char_to_int(time[3])*10 + char_to_int(time[4]));
}

// int (minutes) -> xx:xx
int hour(int v) { return v/60; }
int minutes(int v) { return v%60; }
string h(int v) { return _ + to_string(hour(v)) + ":" + to_string(minutes(v)); }


void init_vertices(void) {
   vertices = (struct vertex*)calloc(nb_stops, sizeof(*vertices));
}

void add_edge(int v, int dst, int travel_time, int departure_time, string trip_id) {
   assert(v < nb_stops);
   int index = vertices[v].nb_edges;
   for(int i = 0; i < index; i++) {
      if(vertices[v].edges[i].dst == dst
            && vertices[v].edges[i].departure_time == departure_time) {
         if(departure_time == -1) // transfer is already in transfer.txt
            return;
         // Update the travel time if for some reason two trains depart at the same time to the same destination but one is faster
         if(vertices[v].edges[i].travel_time > travel_time) {
            vertices[v].edges[i].travel_time = travel_time;
            vertices[v].edges[i].trip_id = trip_id;
         }
         // Ignore edge, it is already there!
         return;
      }
   }
   vertices[v].nb_edges++;
   //vertices[v].edges = (struct edge*)realloc(vertices[v].edges, vertices[v].nb_edges*sizeof(*vertices[v].edges));
   vertices[v].edges.resize(vertices[v].nb_edges);
   vertices[v].edges[index].dst = dst;
   vertices[v].edges[index].travel_time = travel_time;
   vertices[v].edges[index].departure_time = departure_time;
   vertices[v].edges[index].trip_id = trip_id;
}

int is_frequently_cancelled(Calendar *c) {
   return exclude_frequently_cancelled_routes && c && (c->nb_cancellations >= c->nb_expected*50/100);
}

/* Build the graph of all trajectories */
void create_trajectories(char *dir) {
   string trip_file_name = _ + dir + "/stop_times.txt";
   io::CSVReader<5, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(trip_file_name);
   in.read_header(io::ignore_extra_column, "trip_id", "arrival_time", "departure_time", "stop_id","stop_sequence");

   string trip_id, arrival_time, departure_time, stop_id;
   size_t stop_sequence = 0, nb_trips = 0;

   Stop *parent = NULL;
   int previous_time = 0;
   size_t nb_trajectories = stol(exec(_ + "wc -l \"" + trip_file_name + "\""));
   size_t first_stop_sequence = -1;
   size_t last_stop_sequence = -1;
   string last_trip_id = "";
   size_t line_number = 0;
   while(in.read_row(trip_id, arrival_time, departure_time, stop_id, stop_sequence)) {
      line_number++;

      /* 
       * stop_times.txt is a list of times for every trip
       * Check if we are still on the same trip. If not, start a new one
       */
      if(trip_id != last_trip_id) {
         first_stop_sequence = stop_sequence;
         last_trip_id = trip_id;
      } else if(stop_sequence != last_stop_sequence + 1) {
         // Same trip, we are expecting the next stop (sequence + 1)
         // But some french datasets are completely nonsensical, just ignore the buggy trip fully
         continue;
      }
      last_stop_sequence = stop_sequence;

      /* Destination... */
      Stop *current = stops[stop_id];
      if(!current) {
         cerr << "Unknown stop " << stop_id << "\n";
         continue;
      }

      /* Check trip information */
      Trip *t = trips[trip_id];
      if(t && t->is_train)
         current->is_train = 1;

      /* Is the train/bus circulating on the chosen day? */
      Calendar *info = calendar[t->service_id];
      if(!info)
         goto skip;
      if(!t->is_train && trains_only)
         goto skip; 
      if(!info->scheduled_on_chosen_date)
         goto skip;
      if(info->cancelled_on_chosen_date)
         goto skip;
      if(is_frequently_cancelled(info)) // Do not used cancelled trains in sssp
         goto skip;

      if(stop_sequence != first_stop_sequence && parent) {
         int current_time = string_to_time(arrival_time);
         int travel_time = current_time - previous_time;
         if(travel_time < 0) {
            cerr << "Negative travel time " << travel_time << " from " << parent->stop_name << " at " << departure_time << " to " << current->stop_name << " arrival at " << arrival_time << "(" << current_time << ") previous time was " << previous_time << "(" << h(previous_time) << ") line " << line_number << "\n";
            last_stop_sequence = -1; // ignore the rest of the trip
            goto skip;
         }
         if(travel_time == 0)
            travel_time = 1; // avoid infinite loops, in some datasets it takes 30 seconds between 2 stops, but we compute at the minute resolution, so artificially bump the travel time!
         add_edge(parent->id, current->id, travel_time, previous_time, trip_id);
      }

      parent = current;
      previous_time = string_to_time(departure_time);
      if(departure_time < arrival_time) {
         cerr << departure_time << " is before " << arrival_time << " to " << current->stop_name << " line " << line_number << "\n";
         last_stop_sequence = -1; // ignore the rest of the trip
      }

skip:
      nb_trips++;
      if(nb_trips % 300000 == 0)
         cerr << "Adding trajectories " << nb_trips << "/"<< nb_trajectories << " (" << (nb_trips*100/nb_trajectories) << "%)\n";
   }
}

void create_transfers(char *dir) {
   string trip_file_name = _ + dir + "/transfers.txt";
   ifstream trip_file(trip_file_name.c_str());
   if(!trip_file.good())
      return; // transfers file does not always exist

   io::CSVReader<3, trim_chars<' ', '\t'>, double_quote_escape<',','\"'> > in(trip_file_name);
   in.read_header(io::ignore_extra_column, "from_stop_id", "to_stop_id", "min_transfer_time");

   string from_stop_id, to_stop_id;
   int min_transfer_time;

   while(in.read_row(from_stop_id, to_stop_id, min_transfer_time)) {
      Stop *parent = stops[from_stop_id];
      Stop *dst = stops[to_stop_id];
      if(!parent || !dst)
         continue;
      if(parent == dst)
         continue;
      if(min_transfer_time < 60)
         min_transfer_time = 60; // add at least a minute of walking
      if(dst->is_train || !trains_only)
         add_edge(parent->id, dst->id, min_transfer_time/60, -1, string(""));
   }
}

void create_walks(void) {
   size_t nb_loops = stop_ids.size(), done = 0, prev_percent = 0;

   // Create a list of Stops sorted by latitude
   for (auto it1 = stop_ids.begin(); it1 != stop_ids.end(); ++it1) {
      stops_sorted_by_lat.push_back(it1->second);
   }
   stops_sorted_by_lat.sort([](auto a, auto b) { return a->stop_lat < b->stop_lat; });

   // For each stop, find other stops less than 100m away
   for (auto it1 = stops_sorted_by_lat.begin(); it1 != stops_sorted_by_lat.end(); ++it1) {
      Stop *s1 = *it1;
      //cout << "Doing stop " <<  s1->stop_lat << "\n";
      if(!s1->is_train && trains_only)
         continue;

      // Find other stops with a lower latitude (but not too far)
      auto it2 = it1;
      it2--;
      if(it1 != stops_sorted_by_lat.begin()) {
         do {
            Stop *s2 = *it2;
            //cout << "\t 1 - with stop " <<  s2->stop_lat << " train " << s2->is_train << "\n";
            // 1deg = 110km, so no need to go further, all stops are > 100m
            if(s2->stop_lat < s1->stop_lat - 0.001)
               break;
            if(!s2->is_train && trains_only)
               continue; 
            double dst = distanceEarth(s1->stop_lat, s1->stop_lon, s2->stop_lat, s2->stop_lon);
            if(dst < 100) { // less than 100m, add a walk path!
                            //if(s1->stop_name == "Bern, Hauptbahnhof") 
                            //cout << s1->stop_name << "walk to " << s2->stop_name << "\n";
               add_edge(s1->id, s2->id, 2, -1, "");
            }
         } while(it2-- != stops_sorted_by_lat.begin());
      }

      // And above us (but not too far)
      it2 = it1;
      it2++;
      if(it1 != stops_sorted_by_lat.end() && it2 != stops_sorted_by_lat.end()) {
         do {
            Stop *s2 = *it2;
            //cout << "\t 2 - with stop " <<  s2->stop_lat << "\n";
            // 1deg = 110km, so no need to go further, all stops are > 100m
            if(s2->stop_lat > s1->stop_lat + 0.001)
               break;
            if(!s2->is_train && trains_only)
               continue; 
            double dst = distanceEarth(s1->stop_lat, s1->stop_lon, s2->stop_lat, s2->stop_lon);
            if(dst < 100) { // less than 100m, add a walk path!
                            //if(s1->stop_name == "Bern, Hauptbahnhof") 
                            //cout << s1->stop_name << "walk to " << s2->stop_name << "\n";
               add_edge(s1->id, s2->id, 2, -1, "");
            }
         } while(++it2 != stops_sorted_by_lat.end());
      }
      done++;
      if(done*100/nb_loops != prev_percent) {
         prev_percent = done*100/nb_loops;
         cerr << "Adding walks " << done << "/" << nb_loops << " (" << (done * 100 / nb_loops) << "%)\n";
      }
   }
}

// Is a stop near to a train station?
// If so, it will not be printed out in the results because we don't want to clutter the map
// Logic of the code is similar to adding walks in create_walks
void fill_is_close_to_train(void) {
   size_t nb_loops = stop_ids.size(), done = 0, prev_percent = 0;
   for (auto it1 = stops_sorted_by_lat.begin(); it1 != stops_sorted_by_lat.end(); ++it1) {
      Stop *s1 = *it1;
      s1->is_close_to_train = 0;

      // a train is never close to another and we want to keep all of them
      if(s1->is_train)
         continue;

      // Find other stops with a lower latitude (but not too far)
      auto it2 = it1;
      it2--;
      if(it1 != stops_sorted_by_lat.begin()) {
         do {
            Stop *s2 = *it2;
            if(s2->stop_lat < s1->stop_lat - 0.02) // has to be more than 2km away
               break;
            if(s2->is_train) {
               double dst = distanceEarth(s1->stop_lat, s1->stop_lon, s2->stop_lat, s2->stop_lon);
               if(dst < 2000) { // less than 2km
                  s1->is_close_to_train = 1;
                  goto end;
               }
            }
         } while(it2-- != stops_sorted_by_lat.begin());
      }

      // And above us (but not too far)
      it2 = it1;
      it2++;
      if(it1 != stops_sorted_by_lat.end() && it2 != stops_sorted_by_lat.end()) {
         do {
            Stop *s2 = *it2;
            //cout << "\t 2 - with stop " <<  s2->stop_lat << "\n";
            // 1deg = 110km, so no need to go further, all stops are > 100m
            if(s2->stop_lat > s1->stop_lat + 0.02)
               break;
            if(s2->is_train) {
               double dst = distanceEarth(s1->stop_lat, s1->stop_lon, s2->stop_lat, s2->stop_lon);
               if(dst < 2000) { // less than 2km
                  s1->is_close_to_train = 1;
                  goto end;
               }
            }
         } while(++it2 != stops_sorted_by_lat.end());
      }
end:
      done++;
      if(done*100/nb_loops != prev_percent) {
         prev_percent = done*100/nb_loops;
         cerr << "Checking proximity with trains " << done << "/" << nb_loops << " (" << (done * 100 / nb_loops) << "%)\n";
      }
   }
}

int _add_connection(Stop *dst, Source *f) {
   for (auto it = dst->parents.begin(); it != dst->parents.end(); ++it) {
      Source *e = *it;
      // Two trains arrive at the same time...
      if(e->arrival_time == f->arrival_time) {
         if(e->travel_time <= f->travel_time) {
            // New path takes longer to get there, ignore it
         } else if(f->travel_time <= max_travel_time) { // Only add stuff less than 10h away
            // found a shorter way that arrives at the same time!
            assert(e->child == dst);
            e->parent = f->parent;
            e->child = f->child;
            e->travel_time = f->travel_time;
            e->departure_time = f->departure_time;
            e->walking = f->walking;
            e->best = f->best;
            e->edge = f->edge;
            dst->next = 1;
         }
         return 1;
      }
   }

   dst->parents.push_back(f);
   dst->next = 1;
   return 0;
}

/* Add a connection to a vertex -- dst can be reached from the Source f */
void add_connection(Stop *dst, Source *f, int iteration, int print) {
   // Did we find the best time?
   if(dst->best_time == -1 || dst->best_time > f->travel_time) {
      dst->best_time = f->travel_time;
      dst->nb_hops = iteration;
      dst->best_source = f;
   }

   // Add the trajectory to the list of possible trajectories if it is close the the best (less than 60 minutes worse than best)
   if(f->travel_time < dst->best_time + 60 && f->travel_time < dst->best_time * 2) {
      if(print) {
         cout << "Found connection " << f->parent->stop_name << " -> " << dst->stop_name << " at " << h(f->departure_time) << " arrive " << h(f->arrival_time)
            << " best " << h(dst->best_time) << " ours " << h(f->travel_time)
            << " so far " << dst->parents.size() << " trajectories " << "\n";
      }
      int already_there = _add_connection(dst, f);
      if(already_there && dst->best_source != f)
         delete f;
      if(print) {
         cout << "Was there " << already_there << "\n";
         cout << "Now " << dst->parents.size() << " trajectories " << "\n";
         for(auto *s: dst->parents) {
            cout << "Current to " << dst->stop_name << " arrival at " << h(s->arrival_time) << " in " << h(s->travel_time) << "\n";
         }
     }
   } else {
      if(print) {
         cout << "Ignoring connection " << f->child->stop_name << " -> " << dst->stop_name << " at " << h(f->departure_time)
            << " best " << h(dst->best_time) << " ours " << h(f->travel_time)
            << " so far " << dst->parents.size() << " trajectories " << "\n";
      }
      if(dst->best_source != f)
         delete f;
   }
}

/* Get all the trajectories leaving src, and check if it adds new possibilities to the destinations reachable from src */
void __sssp(Stop *src, int iteration) {
   vector<edge> *edges = &vertices[src->id].edges;
   for(size_t i = 0; i < edges->size(); i++) {
      struct edge *e = &(edges->at(i));
      Stop *dst = stop_ids[e->dst];
      if(!dst) {
         cerr << "Bug\n";
         continue;
      }
      if(dst->best_time == 0)// don't go back to the source
         continue;

      Source *f = NULL;
      if(e->departure_time == -1) { // we can use that edge whenever (foot/bike transfer)
                                    // So push the edge for all transfers that end up in src
         for (auto it = src->parents.begin(); it != src->parents.end(); ++it) {
            Source *parent = *it;

            if(parent->parent == dst)// don't loop stupidly! Might happen because of walking back and forth between station and bus stop!
               continue;
            if(parent->walking)// don't walk twice, be lazy (avoid combinatory explosion)
               continue;

            f = new Source();
            f->parent = src;
            f->child = dst;
            f->departure_time = -1;
            f->arrival_time = parent->arrival_time + e->travel_time;
            f->travel_time = parent->travel_time + e->travel_time;
            f->best = parent;
            f->walking = 1;
            f->edge = e;
            add_connection(dst, f, iteration, 0);
         }
      } else {
         Source *best = NULL;
         int best_time = -1;
         // What's the best previous connection that allows us to use that train?
         for (auto it = src->parents.begin(); it != src->parents.end(); ++it) {
            Source *parent = *it;
            // If we are the source of the sssp, then we are always best
            if(parent->parent == NULL) { // source is the only node without parent!
               best = parent;
               best_time = 0;
               break;
            }

            // Ignore impossible connections
            if(e->departure_time < parent->arrival_time)
               continue;
            // Same station, but different train, we need at least a bit of time to switch unless we walked to get there
            if(parent->edge && parent->edge->departure_time != -1 && e->departure_time == parent->arrival_time && e->trip_id != parent->edge->trip_id)
               continue;

            // Time to reach destination is time already spent traveling + waiting time
            int time = parent->travel_time + (e->departure_time - parent->arrival_time);
            if(time < best_time || best_time == -1) {
               best_time = time;
               best = parent;
            }
         }

         // It is possible that we cannot use that connection (too early to be reachable)
         if(!best)
            continue;

         // Otherwise we found a way
         f = new Source();
         f->parent = src;
         f->child = dst;
         f->departure_time = e->departure_time;
         f->arrival_time = e->departure_time + e->travel_time;
         f->travel_time = best_time + e->travel_time;
         f->best = best;
         f->walking = 0;
         f->edge = e;
         add_connection(dst, f, iteration, 0);

      }
   }
}

// Do the  __sssp for all active vertices
void _sssp(int iterations) {
   cerr << "Iteration " << iterations << "\n";

   for (auto it = stop_ids.begin(); it != stop_ids.end(); ++it) {
      Stop *s = it->second;
      if(s->active) {
         __sssp(s, iterations);
      }
   }

   int nb_active = 0;
   for (auto it = stop_ids.begin(); it != stop_ids.end(); ++it) {
      Stop *s = it->second;
      s->active = s->next;
      s->next = 0;
      if(s->active)
         nb_active++;
   }
   if(nb_active && iterations < 200) // and recurse until we don't have any active vertex anymore
      _sssp(iterations + 1);
}

/* Trajectories already outputed */
void output_stop(Source *f) {
   if(!f)
      return;
   Stop *dst = f->child;
   if(!dst)
      return;
   Stop *src = f->parent;
   if(!src)
      return;

   cout << "{ dst:\"" << dst->stop_name << "\", dsttrain:" << dst->is_train <<", dstlat:" << dst->stop_lat << ", dstlon:" << dst->stop_lon << ", src:\"" << src->stop_name << "\", srctrain:" << src->is_train << ", srclat:" << src->stop_lat << ", srclon:" << src->stop_lon<< ", dur:" << f->travel_time << " },\n";
}

int sssp(string origin) {
   /* Best way to get to source is empty route */
   Source *s = new Source();
   s->parent = NULL;
   s->travel_time = 0;
   s->arrival_time = 0;
   s->best = NULL;
   s->walking = 0;
   s->child = NULL;
   s->edge = NULL;

   // Start from all the platforms of origin (possible if it's a train)
   for(auto src: *stop_names[origin]) {
      src->parents.push_back(s);
      src->nb_hops = 0;
      src->best_time = 0;
      src->best_source = NULL;
      src->active = 1;
   }

   /* Run */
   _sssp(0);


   /* Report best times to get to all other stops */
   std::vector<std::pair<int,Stop*>> dst;
   for (auto it = stop_ids.begin(); it != stop_ids.end(); ++it) {
      Stop *s = it->second;
      dst.push_back({s->best_time, s});
   }
   std::sort(dst.begin(), dst.end()); // sort by time
   if(simplify_results)
      fill_is_close_to_train();
   cout << "[ ";
   for (auto it = dst.begin(); it != dst.end(); ++it) {
      Stop *s = it->second;
      if(s->best_time > 0 && s->best_time < max_travel_time && (!simplify_results || !s->is_close_to_train) && (!trains_only || s->is_train)) {
         output_stop(s->best_source);
      }
   }
   cout << "]\n";

   return 0;
}

void best_path(string d) {
   cerr << "-----\n";
   if(!stop_names[d]) {
      cerr << "Wrong name\n";
      return;
   }

   Stop *dst = NULL;
   for(auto i : *stop_names[d]) {
      if(i->best_time == -1)
         continue;
      if(!dst)
         dst = i;
      else if(dst->best_time > i->best_time)
         dst = i;
   }
   if(!dst) {
      cerr << "Stop is not reachable\n";
      return;
   }

   Source *f = dst->best_source;
   while(f) {
      cerr << "Arrival in " << (f->child?f->child->stop_name:"") << " ID " << (f->child?f->child->stop_id:"") << " TRAIN " << dst->is_train <<") at " << h(f->arrival_time) << " in total " << h(f->travel_time) << "\n";
      cerr << "\tDeparture from " << (f->parent?f->parent->stop_name:"") << " (ID " << (f->parent?f->parent->stop_id:"") << ") at " << h(f->departure_time) << (f->walking?"walking":"transport") << "\n";
      f = f->best;
   }
}

int main(int argc, char **argv) {
   if(argc < 4) {
      cout << "Usage: parse <directory containing gtfs data> <stop name> <YYYYmmdd> <trains only?>\n";
      cout << "\tWill compute the shortest travel time from <stop name> to all other stops in the GTFS dataset\n";
      cout << "\tE.g.: ./parse ./swiss_gtfs Lausanne 20221119 1\n";
      cout << "\tE.g.: ./parse ./german_gtfs \"München Hbf\" 20230130 0\n";
      return -1;
   }
   chosen_day = _ + argv[3];
   trains_only = atoi(argv[4]);

   // Parse stops.txt and return the Stop object corresponding to <stop name>
   create_stops(argv[1], argv[2]);

   // routes.txt
   create_routes(argv[1]);

   // trips.txt
   create_trips(argv[1]);

   // calendar.txt
   create_expected_trips(argv[1]);

   // calendar_dates.txt
   create_cancelled_trips(argv[1]);

   // Convert stops into a vertex array
   init_vertices();

   // Parse stop_times.txt
   create_trajectories(argv[1]);

   // Parse transferts.txt
   create_transfers(argv[1]);

   // Connect all stations that are close enough to be walkable
   create_walks();

   // Find shortest travel times from origin point.
   sssp(argv[2]);

   //Test
   best_path("Interlaken Ost");
   best_path("Visp");
   best_path("Kandersteg");
   best_path("Oey-Diemtigen");
   best_path("Bantzenheim");
   best_path("Lyon Perrache");
   best_path("Vouvry");
   best_path("Lausanne");
   best_path("St-Maurice");
   best_path("Beaulieu INSA");
   best_path("La Fouilleuse");
}
