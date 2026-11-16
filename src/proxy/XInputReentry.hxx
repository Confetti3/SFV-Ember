#pragma once

// Which XInput the proxy forwards a call to, by how deep the calling thread
// already is in the proxy. A tool that hooks XInput by name (it resolves the
// original of xinput1_3 through the process's module of that name, which is
// this proxy) can make the proxy and itself call each other until the stack
// overflows. The first call on a thread goes to System32's xinput1_3, a call
// made from inside it (re-entered) to System32's xinput1_4, and one from
// inside that has nothing to go to: the caller gets "not connected".

namespace sfve::proxy {

enum class XInputRoute { Primary, Fallback, None };

inline thread_local int t_depth = 0;

// Lives for the length of one forwarded call.
class ReentryScope {
public:
	ReentryScope() : depth_(t_depth++) {}
	~ReentryScope() { t_depth--; }
	ReentryScope(const ReentryScope&) = delete;
	ReentryScope& operator=(const ReentryScope&) = delete;

	XInputRoute Route() const
	{
		return depth_ == 0 ? XInputRoute::Primary : depth_ == 1 ? XInputRoute::Fallback : XInputRoute::None;
	}

private:
	int depth_;
};

} // namespace sfve::proxy
