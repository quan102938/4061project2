#include "wrapper.h"
#include "util.h"
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <signal.h>

#define MAX_TABS 100    // this gives us 99 tabs, 0 is reserved for the controller
#define MAX_BAD 1000    // # of bad urls allowed in blacklist
#define MAX_URL 100     // max length of url
#define MAX_FAV 100     // max # of favorites
#define MAX_LABELS 100  // IDK yet


comm_channel comm[MAX_TABS];         // Communication pipes 
char favorites[MAX_FAV][MAX_URL];    // Maximum char length of a url allowed
int num_fav = 0;                     // # favorites

typedef struct tab_list 
{
  int free;             // 1 if free, 0 if not
  int pid;              // stored PID when forked
} tab_list;

// Tab bookkeeping
tab_list TABS[MAX_TABS];  


/************************/
/* Simple tab functions */
/************************/

// return total number of tabs
int get_num_tabs () 
{
  int total = 0;
  for(int i = 1; i < MAX_TABS; ++i)
    if(!TABS[i].free)
      ++total;
  return total;
}

// get next free tab index|-1 if no free tabs
int get_free_tab () 
{
  int index = -1;
  for(int i = 1; i < MAX_TABS; ++i)
    if(TABS[i].free)
    {
      index = i;
      break;
    }
  return index;
}

// init TABS data structure
void init_tabs () 
{
  int i;
  for (i=1; i<MAX_TABS; ++i)
    TABS[i].free = 1;
  TABS[0].free = 0;
}

/***********************************/
/* Favorite manipulation functions */
/***********************************/

// return 0 if favorite is ok, -1 otherwise
// both max limit, already a favorite (Hint: see util.h) return -1
int fav_ok (char *uri) 
{
  if(num_fav >= MAX_FAV || on_favorites(uri))
    return -1;
  return 0;
}


// Add uri to favorites file and update favorites array with the new favorite
void update_favorites_file (char *uri) 
{
  // Add uri to favorites file

  FILE *favorites_file = fopen("./.favorites", "w");
  if(favorites_file == NULL)
  {
    printf("favorites file could not be opened!\n");
  }

  if(fputs(uri, favorites_file) < 0)
  { 
    perror("failed to write to file");
    exit(1);
  }

  fclose(favorites_file);

  // Update favorites array with the new favorite
  strcpy(favorites[num_fav], uri);
  ++num_fav;
}

// Set up favorites array
void init_favorites (char *fname) 
{
  FILE *favorites_file = fopen("./.favorites", "w");
  if(favorites_file == NULL)
  {
    printf("favorites file could not be opened!\n");
  }

  while(!feof(favorites_file))
  {
    char* temp = NULL;
    fgets(temp, MAX_URL, favorites_file); // fgets(favorites[num_fav], MAX_URL, favorites_file)
    strcpy(favorites[num_fav], temp); 
    ++num_fav;
  }

  fclose(favorites_file);
}

// Make fd non-blocking just as in class!
// Return 0 if ok, -1 otherwise
// Really a util but I want you to do it :-)
int non_block_pipe (int fd) //non_block_pipe(comm[index].inbound[0])
{
  int flags = 0;
  fcntl(fd, flags, 0); // LOOK AT!!!
  if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    return -1;
  return 0;
}

/***********************************/
/* Functions involving commands    */
/***********************************/

// Checks if tab is bad and url violates constraints; if so, return.
// Otherwise, send NEW_URI_ENTERED command to the tab on inbound pipe
void handle_uri (char *uri, int tab_index) 
{
  if(on_blacklist(uri) || bad_format(uri) || (get_free_tab() == -1))
  {
    return;
  }
  else
    write(comm[tab_index].inbound[1], NEW_URI_ENTERED, sizeof(NEW_URI_ENTERED));
}


// A URI has been typed in, and the associated tab index is determined
// If everything checks out, a NEW_URI_ENTERED command is sent (see Hint)
// Short function
void uri_entered_cb (GtkWidget* entry, gpointer data) 
{
  printf("uri_entered_cb\n");
  if(data == NULL) 
  {	
    return;
  }

  // Get the tab (hint: wrapper.h)

  // query_tab_id_for_request(entry, data);

  // Get the URL (hint: wrapper.h)

  // get_entered_uri(entry);

  // Hint: now you are ready to handle_the_uri

  handle_uri(get_entered_uri(entry), query_tab_id_for_request(entry, data));

}
  

// Called when + tab is hit
// Check tab limit ... if ok then do some heavy lifting (see comments)
// Create new tab process with pipes
// Long function
void new_tab_created_cb (GtkButton *button, gpointer data) 
{
  printf("new_tab_created_cb\n");
  if (data == NULL) 
  {
    return;
  }

  // at tab limit?

  if(get_free_tab() == -1)
  {
    perror("At Tab Limit\n");
    exit(1);
  }
  // Get a free tab

  int tab_index = get_free_tab();

  // Create communication pipes for this tab
  
  if(pipe(comm[tab_index].inbound) == -1)
  {
    perror("Error Creating Pipes\n");
    exit(1);
  }
  if(pipe(comm[tab_index].outbound) == -1)
  {
    perror("Error Creating Pipes\n");
    exit(1);
  }

  // Make the read ends non-blocking 

  if(non_block_pipe(comm[tab_index].inbound[0]) == -1)
  {
    perror("Pipe Error\n");
    exit(1);
  }
  if(non_block_pipe(comm[tab_index].outbound[0]) == -1)
  {
    perror("Pipe Error\n");
    exit(1);
  }

  // fork and create new render tab
  // Note: render has different arguments now: tab_index, both pairs of pipe fd's
  // (inbound then outbound) -- this last argument will be 4 integers "a b c d"
  // Hint: stringify args

  TABS[tab_index].pid = fork();                      //create a child
  if (TABS[tab_index].pid == -1) 
  {
    perror("fork() failed");
    exit(0);
  }
  else if (TABS[tab_index].pid == 0)
  { 
    char exec_args[sizeof(int)*5]; // "tab_index comm[tab_index].in/outbound[0/1] x 4"
    sprintf(exec_args,"%d %d %d %d %d",tab_index, comm[tab_index].inbound[0],
      comm[tab_index].inbound[1], comm[tab_index].outbound[0], comm[tab_index].outbound[1]);
    execl("./render", "render", exec_args, NULL);    //render the url
    perror("open failed");  
    exit(0);
  }
  else
  {
    TABS[tab_index].free = 0;
  }

  // Controller parent just does some TABS bookkeeping
}

// This is called when a favorite is selected for rendering in a tab
// Hint: you will use handle_uri ...
// However: you will need to first add "https://" to the uri so it can be rendered
// as favorites strip this off for a nicer looking menu
// Short
void menu_item_selected_cb (GtkWidget *menu_item, gpointer data) 
{

  if (data == NULL) {
    return;
  }
  
  // Note: For simplicity, currently we assume that the label of the menu_item is a valid url
  // get basic uri
  char *basic_uri = (char *)gtk_menu_item_get_label(GTK_MENU_ITEM(menu_item));

  // append "https://" for rendering
  char uri[MAX_URL];
  sprintf(uri, "https://%s", basic_uri);

  // Get the tab (hint: wrapper.h)

  int tab = query_tab_id_for_request(menu_item, data);

  // Hint: now you are ready to handle_the_uri

  handle_uri(uri, tab);

  return;
}


// BIG CHANGE: the controller now runs an loop so it can check all pipes
// Long function
int run_control() 
{
  browser_window * b_window = NULL;
  int i, nRead;
  req_t req;

  //Create controller window
  create_browser(CONTROLLER_TAB, 0, G_CALLBACK(new_tab_created_cb),
		 G_CALLBACK(uri_entered_cb), &b_window, comm[0]);

  // Create favorites menu
  create_browser_menu(&b_window, &favorites, num_fav);
  printf("menu created\n");
  
  while (1) {
    printf("before while\n");
    process_single_gtk_event();
    printf("while\t");

    // Read from all tab pipes including private pipe (index 0)
    // to handle commands:
    // PLEASE_DIE (controller should die, self-sent): send PLEASE_DIE to all tabs
    // From any tab:
    //    IS_FAV: add uri to favorite menu (Hint: see wrapper.h), and update .favorites
    //    TAB_IS_DEAD: tab has exited, what should you do?

    // Loop across all pipes from VALID tabs -- starting from 0
    for (i=0; i<MAX_TABS; i++) 
    {
      if(TABS[i].free) continue;

      int tmp = nRead;
      nRead = read(comm[i].outbound[0], &req, sizeof(req_t));
      if(tmp == nRead) continue;
      // Check that nRead returned something before handling cases

      // Case 1: PLEASE_DIE
      if(nRead == PLEASE_DIE)
      {
        for(int j=0;j<MAX_TABS; ++j)
          write(comm[j].inbound[1], "PLEASE_DIE", sizeof(PLEASE_DIE));
        break;
      }
      // Case 2: TAB_IS_DEAD
	    if(nRead == TAB_IS_DEAD)
      {
          
      }
      // Case 3: IS_FAV
    }
    usleep(1000);
  }
  return 0;
}


int main(int argc, char **argv)
{

  if (argc != 1) {
    fprintf (stderr, "browser <no_args>\n");
    exit (0);
  }

  init_tabs ();
  // init blacklist (see util.h), and favorites (write this, see above)

  // make sure favorites and blacklist are initialized

  init_blacklist("./.blacklist");

  // Fork controller
  // Child creates a pipe for itself comm[0]
  // then calls run_control ()
  // Parent waits ...

  TABS[0].pid = fork();                      //create a child
  if (TABS[0].pid == -1) 
  {
    perror("fork() failed");
    exit(0);
  }
  else if (TABS[0].pid == 0)
  { 
    if(pipe(comm[0].inbound) == -1)
    {
      perror("Error Creating Pipes\n");
      exit(1);
    }
    if(pipe(comm[0].outbound) == -1)
    {
      perror("Error Creating Pipes\n");
      exit(1);
    }
    run_control();
  }
  else
    wait(NULL);
return 0;
}
