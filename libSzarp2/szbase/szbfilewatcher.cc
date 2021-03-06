/* 
  SZARP: SCADA software 

*/
/* 
 * szbase - szbfilewatcher
 * $Id$
 * <schylek@praterm.com.pl>
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <boost/thread/thread.hpp>
#include <boost/thread/xtime.hpp>
#include "szbfilewatcher.h"
#include "liblog.h"
#include "szbfile.h"

class WatchEntryPredicate {
	public:

		std::wstring condition;

		WatchEntryPredicate(std::wstring prefix):condition(prefix){};
	
		bool operator()(WatchEntry w) {
			return condition == w.prefix;
		}
};

Watcher::Watcher( SzbFileWatcher * watcher ): m_watcher(watcher){};

void Watcher::operator()() {

	try {

		do {
			boost::this_thread::sleep(boost::posix_time::minutes(15));
			m_watcher->CheckModifications();
		
		} while(1);

	} catch (boost::thread_interrupted) {
		sz_log(10, "File watcher thread terminated");
		return;
	} catch (...) {
		sz_log(10, "Unknown excpetion thrown in watcher thread");
		return;
	}


}

SzbFileWatcher::SzbFileWatcher(): start_flag(false) {}


void
SzbFileWatcher::Terminate() {
/* this does not work under MSW (app crashes), according to boost mailing list
archives, mingw should be compiled with support for dwarf2 exceptions (and not sjlj).
However, commenting this out seems to be harmless...*/
#ifndef MINGW32
	sz_log(10, "Terminating file watcher thread");
	if (start_flag) {
		thread.interrupt();
		sz_log(10, "After terminating");
		thread.join();
	}
	sz_log(10, "File watcher thread terminated");
#endif
}

void
SzbFileWatcher::AddFileWatch(std::wstring file, std::wstring prefix, void (*callback)(std::wstring, std::wstring)) {

	boost::mutex::scoped_lock datalock(datamutex);

	if (!start_flag) {
		start_flag = true;
		thread = boost::thread(Watcher(this));
	}

	WatchEntry w;
	w.file = file;
	w.prefix = prefix;
	w.callback = callback;

	bool result = szb_file_exists ( file.c_str(), &w.last_mod_date);
	if (!result)
		w.last_mod_date = -1;

	watchlist.push_back(w);

}

void
SzbFileWatcher::CheckModifications() {
	boost::mutex::scoped_lock datalock(datamutex);

	std::list<WatchEntry>::iterator i;
	for (i = watchlist.begin(); i != watchlist.end(); i++) {
		time_t tmp;
		bool result = szb_file_exists ( i->file.c_str(), &tmp);
		if (result && tmp > i->last_mod_date ) {
			i->last_mod_date = tmp;
			i->callback(i->file, i->prefix);
		}
	}

}

void
SzbFileWatcher::RemoveFileWatch( std::wstring prefix ) {
	boost::mutex::scoped_lock datalock(datamutex);

	WatchEntryPredicate pred(prefix);
	watchlist.remove_if(pred);
	
}

