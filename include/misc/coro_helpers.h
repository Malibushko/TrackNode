#pragma once

template<class Coro>
void run_to_completion(Coro task)
{
	while (!task.done())
	{
		task.poll();
	}
}