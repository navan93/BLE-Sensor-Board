#include "sysinit/sysinit.h"
#include "os/os.h"
#include "console/console.h"
#include "log/log.h"


/* Define task stack and task object */
#define MY_TASK_PRI         (1)
#define MY_STACK_SIZE       (512)
struct os_task my_task;
os_stack_t my_task_stack[MY_STACK_SIZE];

#define LOG_MODULE_MAIN (128)

/* This is the task function */
void my_task_func(void *arg) {

    /* The task is a forever loop that does not return */
    while (1) {
        /* Wait one second */
        os_time_delay(os_time_ms_to_ticks32(1000));
        /* Print "Hello World!" to the console */
        console_printf("Hello World!\n");
        MODLOG_INFO(LOG_MODULE_MAIN, "Hello World!\n");
    }
}

int
main(int argc, char **argv)
{
    /* Initialize all packages. */
    sysinit();

    console_printf("Hello World Example Application\n");
    MODLOG_INFO(LOG_MODULE_MAIN, "Hello World Example Application");


    /* Initialize the task */
    int rc = os_task_init(&my_task, "my_task", my_task_func, NULL, MY_TASK_PRI,
                 OS_WAIT_FOREVER, my_task_stack, MY_STACK_SIZE);
    assert(rc == 0);

    /* As the last thing, process events from default event queue. */
    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
