#include "gtest/gtest.h"
#include <afina/Executor.h>

using namespace Afina;
using namespace std;

TEST(ExecutorTest, ConstructDestroy) {
	{
		Executor executor{"",1};
		ASSERT_TRUE(1);
	}
	ASSERT_TRUE(1);
}
