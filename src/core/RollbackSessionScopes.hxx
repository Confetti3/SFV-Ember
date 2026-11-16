#pragma once

// RollbackSession's scoped helpers, for its own .cxx files only: the session
// published to GGPO's callbacks for the length of a call, and the hashing of
// the saves a call made final as it returns.

#include "core/RollbackSession.hxx"

namespace sfve {

// Publishes the session making a GGPO call to the static callbacks.
class RollbackSession::ActiveScope {
public:
	explicit ActiveScope(RollbackSession* s) : previous_(s_active) { s_active = s; }
	~ActiveScope() { s_active = previous_; }
	ActiveScope(const ActiveScope&) = delete;
	ActiveScope& operator=(const ActiveScope&) = delete;

private:
	RollbackSession* previous_;
};

// Hashes the saves a session call made final (HashConfirmed), however the
// call returns: GGPO confirms frames inside ggpo_idle and ggpo_advance_frame,
// and the caller may read HashAt or record a checkpoint right after.
class RollbackSession::ConfirmedOnReturn {
public:
	explicit ConfirmedOnReturn(RollbackSession& s) : s_(s) {}
	~ConfirmedOnReturn()
	{
		s_.rollingBack_ = false; // a GGPO call returns only once its rollbacks are done
		s_.HashConfirmed();
	}
	ConfirmedOnReturn(const ConfirmedOnReturn&) = delete;
	ConfirmedOnReturn& operator=(const ConfirmedOnReturn&) = delete;

private:
	RollbackSession& s_;
};

} // namespace sfve
