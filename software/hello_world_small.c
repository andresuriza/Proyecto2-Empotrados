#include "io.h"
#include "system.h"

#include <stdint.h>
#include <stdio.h>

#define MAILBOX_BASE 0xF000

int main()
{
	volatile uint32_t *mailbox = (volatile uint32_t*) (SHARED_MEM_BASE + MAILBOX_BASE);

	while(1)
	{
		if(mailbox[0] == 1)
		{
		    printf("Play track %d\n", mailbox[1]);

		    mailbox[0] = 0;
		}
	}

	return 0;
}

