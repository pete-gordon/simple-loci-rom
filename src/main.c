#include <loci.h>
#include "keyboard.h"
#include "tui.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "libsrc/dir.h"
#include "libsrc/dirent.h"
#include "persist.h"
#include "filemanager.h"

extern uint8_t irq_ticks;
#pragma zpsym ("irq_ticks")

char txt_title[40];
const char txt_spinner[] = "/-\\|";
const char txt_warn_sign[] = "\x01!\x03";
const char txt_booting[] = "Booting";
const char txt_returning[] = "Returning";
const char txt_help1[] = "\1Cursor keys\3and\1SPACE\3to select item";
const char txt_help2a[] = "\1ESC\3to exit,\1DEL\3to reboot";
const char txt_help2b[] = "\1DEL\3to reboot";

char tmp_str[256];

#define DIR_BUF_SIZE 3072
char dir_buf[DIR_BUF_SIZE];
char** dir_ptr_list = (char **)&dir_buf[DIR_BUF_SIZE];  //Reverse array
unsigned int dir_entries;
int dir_offset;
char dir_lpage[2];
char dir_rpage[2];
uint8_t dir_needs_refresh;
const char txt_dir_warning[] = "Too many files. Use dirs!";


#define HEADER_SIZE 3
#define DIR_PAGE_SIZE (25-HEADER_SIZE)

bool return_possible;

char* dbg_status = TUI_SCREEN_XY_CONST(35,1);
#define DBG_STATUS(fourc) strcpy(dbg_status,fourc)

struct _loci_cfg loci_cfg;

extern void init_display(void);

void main(void);

#define UI_FILE_START 7
tui_widget ui[UI_FILE_START+DIR_PAGE_SIZE+1] = {
    { TUI_START,  1, 0, 0, 0 },
    { TUI_TXT,   20, 0,20, txt_title },
    { TUI_TXT,    0, 1,40, txt_help1 },
    { TUI_TXT,    0, 2,40, txt_help2a },
    { TUI_TXT,    1, HEADER_SIZE,25, loci_cfg.path},
    { TUI_TXT,   33, HEADER_SIZE+DIR_PAGE_SIZE+1, 1, dir_lpage},
    { TUI_TXT,   34, HEADER_SIZE+DIR_PAGE_SIZE+1, 1, dir_rpage},
    { TUI_END,    0, 0, 0, 0 }
};

#define IDX_TITLE 1
#define IDX_HELP2 3
#define IDX_LPAGE 5
#define IDX_RPAGE 6


tui_widget warning[] = {
    { TUI_START, 4,10, 0, 0},
    { TUI_BOX,  32, 3, 0, 0},
    { TUI_INV,   1, 1,  3, txt_warn_sign},
    { TUI_TXT,   5, 1, 25, txt_dir_warning},
    { TUI_END,   0, 0, 0, 0}
};

// TUI_BOX is for drawing around a popup UI, so we just manaully draw a box
void print_filebox(void)
{
    int i;

    for (i=1; i<40; i++) {
       *((char*)(0xbb80+40*HEADER_SIZE+i)) = ((i>1) ? ((i<39) ? '-' : '`') : '^');
       *((char*)(0xbb80+40*(HEADER_SIZE+DIR_PAGE_SIZE+1)+i)) = ((i>1) ? ((i<39) ? '-' : 0x7f) : '~');
    }

    for (i=HEADER_SIZE+1; i<(HEADER_SIZE+DIR_PAGE_SIZE+1); i++) {
        *((char*)(0xbb81+40*i)) = '|';
        memset((void*)(0xbb82+40*i), ' ', 38);
        *((char*)(0xbba7+40*i)) = '|';
    }
}

// dir_cmp -- compare directory entries
int dir_cmp(const void *lhsp,const void *rhsp)
{
    const char *lhs = *((const char**)lhsp);
    const char *rhs = *((const char**)rhsp);
    int cmp;

    cmp = stricmp(lhs,rhs);

    //Sort dirs before files by inverting result
    if(lhs[0] != rhs[0]){
        return -cmp;
    }else{
        return cmp;
    }
}

bool ends_in(const char *file, const char *ext)
{
    int len, elen;

    len = strlen(file);
    elen = strlen(ext);
    if (len < elen)
        return false;

    return strcasecmp(&file[len-elen], ext) == 0;
}

/* Fill the directory buffer with filenames from the bottom
   and pointers from the top.
   Returns 0 if buffer becomes full before all dir entries are captured
*/
uint8_t dir_fill(char* dname){
    DIR* dir;
    struct dirent* fil;
    uint16_t tail;     //Filename buffer tail
    uint8_t ret;
    int len;

    if(!dir_needs_refresh){
    //    return 1;
    }
    //DBG_STATUS("odir");
    dir = opendir(dname);
    if(dname[0]==0x00){     //Root/device list
        tail = 0;
        dir_entries = 0;
    }else{                  //Non-root
        strcpy(dir_buf,"/..");
        dir_ptr_list[-1] = dir_buf;
        tail = 4; //strlen("/..")+1
        dir_entries = 1;
    }
    dir_offset = 0;
    ret = 1;
    //DBG_STATUS("rdir");
    while(tail < DIR_BUF_SIZE){             //Safeguard
        do {
            fil = readdir(dir);
        }while(fil->d_name[0]=='.');        //Skip hidden files
        if(fil->d_name[0] == 0){            //End of directory listing
            break;
        }
        dir_ptr_list[-(++dir_entries)] = &dir_buf[tail];  
        if(fil->d_attrib & DIR_ATTR_DIR){
            dir_buf[tail++] = '/';
        }else if(fil->d_attrib & DIR_ATTR_SYS){
            dir_buf[tail++] = '[';
        }else{
            if ((!ends_in(fil->d_name, ".dsk")) && (!ends_in(fil->d_name, ".tap"))) {
                dir_entries--;  //roll-back
                continue;       //next file
            }
            dir_buf[tail++] = ' ';
        }
        len = strlen(fil->d_name);
        if(len > (DIR_BUF_SIZE-tail-(dir_entries*sizeof(char*)))){
            ret = 0;                     //Buffer is full
            dir_entries--;                //Rewind inclomplete entry
            break;
        }else{
            strcpy(&dir_buf[tail], fil->d_name);
            tail += len + 1;
        }
    }
    //DBG_STATUS("cdir");
    closedir(dir);
    //DBG_STATUS("    ");

    qsort(&dir_ptr_list[-(dir_entries)], dir_entries, sizeof(char*), dir_cmp);
    dir_needs_refresh = 0;
    return ret;
}

void parse_files_to_widget(void){
    uint8_t i;
    char** dir_idx;
    tui_widget* widget;

    //Directory page out-of-bounds checks
    if(dir_offset >= dir_entries){
        dir_offset -= DIR_PAGE_SIZE;
    }
    if(dir_offset < 0){
        dir_offset = 0;
    }
    dir_idx = &dir_ptr_list[-(dir_entries-dir_offset)]; //(char**)(dir_ptr_list - dir_entries + offset);
    widget = &ui[UI_FILE_START];

    for(i=0; (i < DIR_PAGE_SIZE) && ((i+dir_offset) < dir_entries); i++){
        widget->type = TUI_SEL;
        widget->x = 1;
        widget->y = i+4;
        widget->len = 34;
        widget->data = dir_idx[i]; //dir_ptr_list[-(dir_entries-offset-i)];
        widget = &widget[1];
    }
    widget->type = TUI_END;

    dir_lpage[0] = '-';
    dir_rpage[0] = '-';
    ui[IDX_LPAGE].type = TUI_TXT;
    ui[IDX_RPAGE].type = TUI_TXT;
    if(dir_offset > 0){
        dir_lpage[0] = '<';
        ui[IDX_LPAGE].type = TUI_SEL;
    }
    if(dir_offset+DIR_PAGE_SIZE < dir_entries){
        dir_rpage[0] = '>';
        ui[IDX_RPAGE].type = TUI_SEL;
    }
    
}

uint8_t dir_ok = 0;

void update_dir_ui(void){
    dir_needs_refresh = true;
    dir_ok = dir_fill(loci_cfg.path);
    parse_files_to_widget();
    print_filebox();
    tui_draw(ui);
    if(dir_entries)
        tui_set_current(UI_FILE_START);
    if(!dir_ok){
        tui_draw(warning);
    }
}

void boot(bool do_return){
    char* boot_text;
    if(do_return && !return_possible)
        return;
    if(do_return)
        boot_text = (char*)&txt_returning;
    else
        boot_text = (char*)&txt_booting;
    tui_cls(3);
    strcpy(TUI_SCREEN_XY_CONST(17,14),boot_text);
    loci_cfg.tui_pos = tui_get_current();
    persist_set_loci_cfg(&loci_cfg);
    persist_set_magic();
    mia_set_ax(0x80 | (loci_cfg.ald_on <<4) | (loci_cfg.bit_on <<3) | (loci_cfg.b11_on <<2) | (loci_cfg.tap_on <<1) | loci_cfg.fdc_on);
    //mia_set_ax(0x00 | (loci_cfg.b11_on <<2) | (loci_cfg.tap_on <<1) | loci_cfg.fdc_on);
    VIA.ier = 0x7F;         //Disable VIA interrupts
    if(do_return)
        mia_restore_state();
    else{
        mia_clear_restore_buffer();
        mia_call_int_errno(MIA_OP_BOOT);    //Only returns if boot fails
    }
    VIA.ier = 0xC0;
    tui_cls(3);
    print_filebox();
    tui_draw(ui);
    DBG_STATUS("!ROM");
}


void DisplayKey(unsigned char key)
{
    char* tmp_ptr;
    char* ret;
    int drive;

    switch(key){
        case(KEY_UP):
            tui_prev_active();
            break;

        case(KEY_DOWN):
            tui_next_active();
            break;

        case(KEY_LEFT):
            dir_offset -= DIR_PAGE_SIZE;
            parse_files_to_widget();
            print_filebox();
            tui_draw(ui);
            if(dir_entries)
                tui_set_current(UI_FILE_START);
            break;

        case(KEY_RIGHT):
            dir_offset += DIR_PAGE_SIZE;
            parse_files_to_widget();
            print_filebox();
            tui_draw(ui);
            if(dir_entries)
                tui_set_current(UI_FILE_START);
            break;

        case(KEY_ESCAPE):
            boot(true);
            break;

        case(KEY_DELETE):
            umount(0);
            umount(4);
            loci_cfg.ald_on = 0;
            loci_cfg.tap_on = 0;
            loci_cfg.fdc_on = 0;
            boot(false);
            break;

        case(KEY_SPACE):
        case(KEY_RETURN):
            //Exit from warning
            if(!dir_ok){    
                dir_ok = 1;
                tui_clear_box(1);
                print_filebox();
                tui_draw(ui);
                tui_set_current(UI_FILE_START);
                break;
            }

            switch(tui_get_current()){
                case(IDX_LPAGE):
                    dir_offset -= DIR_PAGE_SIZE;
                    parse_files_to_widget();
                    print_filebox();
                    tui_draw(ui);
                    if(dir_entries)
                        tui_set_current(UI_FILE_START);
                    break;
                case(IDX_RPAGE):
                    dir_offset += DIR_PAGE_SIZE;
                    parse_files_to_widget();
                    print_filebox();
                    tui_draw(ui);
                    if(dir_entries)
                        tui_set_current(UI_FILE_START);
                    break;
                default:
                    //Selection from the list
                    tmp_ptr = (char*)tui_get_data(tui_get_current());
                    if(tmp_ptr[0]=='/' || tmp_ptr[0]=='['){    //Directory or device selection
                        if(tmp_ptr[0]=='['){
                            loci_cfg.path[0] = tmp_ptr[1];
                            loci_cfg.path[1] = tmp_ptr[2];
                            loci_cfg.path[2] = 0x00;
                        }else if(tmp_ptr[1]=='.'){              //Go back down (/..)
                            if((ret = strrchr(loci_cfg.path,'/')) != NULL){
                                ret[0] = 0x00;
                            }else{
                                loci_cfg.path[0] = 0x00;
                            }
                        }else{
                            strncat(loci_cfg.path,tmp_ptr,256-strlen(loci_cfg.path));
                        }
                        update_dir_ui();
                        break;
                    }
                    //File selection
                    tmp_ptr = tmp_ptr + 1;      //adjust for leading space

                    drive = ends_in(tmp_ptr, ".tap") ? 4 : 0;
                    if (mount(drive, loci_cfg.path, tmp_ptr) == 0) {
                        loci_cfg.mounts |= 1u << drive;
                        umount(drive ^ 4);
                        loci_cfg.mounts &= ~1u << (drive^4);

                        loci_cfg.ald_on = drive ? 1 : 0;
                        loci_cfg.tap_on = drive ? 1 : 0;
                        loci_cfg.fdc_on = drive ? 0 : 1;
    
                        boot(false);
                    }else{
                        loci_cfg.mounts &= ~1u << drive;
                    }
            }
            break;
    }
}

void main(void){
    tui_cls(3);
    init_display();

    sprintf(txt_title,"\2LOCI FW %d.%d.%d\3",
        locifw_version[2], locifw_version[1], locifw_version[0]);

    return_possible = mia_restore_buffer_ok();
    ui[IDX_TITLE].x = 39-strlen(txt_title);
    ui[IDX_HELP2].data = return_possible ? txt_help2a : txt_help2b;

    if(!persist_get_loci_cfg(&loci_cfg)){
        loci_cfg.fdc_on = 0x00;
        loci_cfg.tap_on = 0x00;
        loci_cfg.bit_on = 0x00;
        loci_cfg.mou_on = 0x00;
        loci_cfg.b11_on = 0x01;
        loci_cfg.ser_on = 0x01;
        loci_cfg.ald_on = 0x00;
        loci_cfg.mounts = 0x00;
        loci_cfg.path[0] = 0x00;
        loci_cfg.drv_names[0][0] = 0x00;
        loci_cfg.drv_names[1][0] = 0x00;
        loci_cfg.drv_names[2][0] = 0x00;
        loci_cfg.drv_names[3][0] = 0x00;
        loci_cfg.drv_names[4][0] = 0x00;
        loci_cfg.drv_names[5][0] = 0x00;
        loci_cfg.tui_pos = 0;
    }

    update_dir_ui();

    InitKeyboard();


    while(1){
        unsigned char key = ReadKeyNoBounce();
        if(key)
            DisplayKey(key);
        TUI_PUTC_CONST(39,0,txt_spinner[(irq_ticks & 0x03)]);
    }
}
