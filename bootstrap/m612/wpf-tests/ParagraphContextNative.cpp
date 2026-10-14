// SPDX-License-Identifier: MIT
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#include <algorithm>
#include <climits>
#include <unordered_set>
#include <unordered_map>
#include "ParagraphServicesNative.h"
static int creates=0,destroys=0,fail_id=-1;
static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static int WINAPI create_object(void* client,void* context,FSCBKOBJ* callbacks,uint32_t flags,int id,void** output) {
    if(client!=reinterpret_cast<void*>(55)||!context||!callbacks||flags!=0x140||id<0||id>1)return PTS_tserrInvalidParameter;
    ++creates;*output=nullptr;if(id==fail_id)return PTS_tserrCallbackException;
    *output=reinterpret_cast<void*>(100+id);return 0;
}
static int WINAPI destroy_object(void* value){if(value!=reinterpret_cast<void*>(100)&&value!=reinterpret_cast<void*>(101))return PTS_tserrInvalidParameter;++destroys;return 0;}
static int lines_created=0,lines_destroyed=0,breaks_destroyed=0,clients_created=0,clients_destroyed=0,fail_line=-1,columns=1,available_height=80,invalid_columns=false,hard_break=0,keep_mode=0,keep_next=0,para_count=2;
static bool containers=false,lose_subtrack=false;
static bool attached_figure=false,figure_fail_properties=false,figure_fail_position=false,inside_figure=false;
static int figure_alignment=0;
static std::unordered_map<void*,void*> figure_content;
static bool structural_line=false;
static bool subpage_parent=false,table_mode=false,inside_table_cell=false,lose_table_cell=false;
static int table_cells_created=0,table_cells_destroyed=0,table_fail_cell=-1,table_rows_count=3;
static bool split_table_cells=false,short_table_cell=false,table_spans=false,span_prefix=false,malformed_span=false;
static int span_set_height=0;
static bool long_span=false,long_table_cell=false;
static int table_records_created=0,table_records_destroyed=0;
static int(WINAPI* table_handler)(const FSTABLEOBJINIT*,void*)=nullptr;
static int(WINAPI* clear_cell_subpage)(void*,void*)=nullptr;
struct MockTableCell {void* page;};
using NativeSubpageFinite=int(WINAPI*)(void*,void*,int,void*,void*,int,int,uint32_t,int,int,const FSRECT*,int,const FSCOLUMNINFO*,int,int,void* const*,const int*,int,const int*,const int*,int,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA,FSFMTR*,void**,void**,int*,FSBBOX*,void**,int*);
static NativeSubpageFinite create_subpage=nullptr;
static int(WINAPI* destroy_subpage)(void*,void*)=nullptr;
static int(WINAPI* destroy_subpage_record)(void*,void*)=nullptr;
static bool block_ui=false,float_obstacle=false,fail_float=false,attached_float=false,attached_right=false,multiple_float=false,multiple_opposite=false;
static int inline_anchor_mode=0;
static bool deferred_attachment=false,split_attachment=false;
static int float_records_destroyed=0,float_records_created=0;
static bool paginate_float=false;
struct MockFloaterRecord {int stage;};
static std::unordered_set<void*> float_live_records;
static int float_created=0,float_destroyed=0,float_fail_at=-1;
static int(WINAPI* floater_handler)(const FSFLOATERINIT*,void*)=nullptr;
static void* page_context=nullptr;
using SubtrackFormat=int(WINAPI*)(void*,void*,int,void*,int,void*,void*,int,int,uint32_t,const FSRECT*,void*,FSKCLEAR,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA,FSFMTR*,void**,void**,int*,FSBBOX*,void**,FSKCLEAR*,int*);
static SubtrackFormat format_subtrack=nullptr;
static int(WINAPI* destroy_subtrack)(void*,void*)=nullptr;
static int(WINAPI* destroy_subtrack_record)(void*,void*)=nullptr;
static int WINAPI container_format(void*,void*,void* input,int previous,void* name,int area,void* footnote,void* geometry,int empty,int suppress,uint32_t direction,FSRECT* rect,void* margin,FSKCLEAR clear,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA suppress_break,int,
    FSFMTR* formatted,void** output,void** br,int* used,FSBBOX* bbox,void** margin_out,FSKCLEAR* clear_out,int* top,int* can_break) {
    *can_break=0;
    int status=format_subtrack(page_context,input,previous,name,area,footnote,geometry,empty,suppress,direction,rect,margin,clear,suppress_break,formatted,output,br,used,bbox,margin_out,clear_out,top);
    if(!status&&lose_subtrack){*output=*br=nullptr;return PTS_tserrCallbackException;}
    return status;
}
static int WINAPI container_destroy_record(void*,void* record){return destroy_subtrack_record(page_context,record);}
static int WINAPI container_destroy(void* track){return destroy_subtrack(page_context,track);}
static int WINAPI subpage_format(void*,void*,void* input,int previous,void* name,int,void*,void*,int empty,int suppress,uint32_t direction,FSRECT* rect,void*,FSKCLEAR,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA suppress_break,int,
    FSFMTR* formatted,void** output,void** br,int* used,FSBBOX* bbox,void** margin_out,FSKCLEAR* clear_out,int* top,int* can_break) {
    *clear_out=fskclearNone;*can_break=0;FSCOLUMNINFO col{0,rect->du};
    int status=create_subpage(page_context,input,previous,name,nullptr,empty,suppress,direction,rect->u+rect->du,rect->v+rect->dv,rect,1,&col,0,0,nullptr,nullptr,0,nullptr,nullptr,0,suppress_break,formatted,output,br,used,bbox,margin_out,top);
    if(!status&&formatted->fForcedProgress)*used=std::max(*used,bbox->fsrc.v+bbox->fsrc.dv-rect->v);
    if(!status&&lose_subtrack){*output=*br=nullptr;return PTS_tserrCallbackException;}return status;
}
static int WINAPI subpage_destroy(void* page){return destroy_subpage(page_context,page);}
static int WINAPI subpage_destroy_record(void*,void* record){return destroy_subpage_record(page_context,record);}
static int WINAPI page_geometry(void*,void*,uint32_t* direction,int* header,int* width,int* height,FSRECT* margin) {
    *direction=0;*header=0;*width=columns==2?240:200;*height=140;*margin={10,20,columns==2?210:100,available_height};return 0;
}
static int WINAPI section_properties(void*,void*,int* page,uint32_t* direction,int* balance,int* cols,int* segments,int* heights) {
    *page=0;*direction=0;*balance=0;*cols=columns;*segments=*heights=0;return 0;
}
static int WINAPI column_info(void*,void*,uint32_t,int capacity,FSCOLUMNINFO* output,int* actual) {
    if(capacity!=2)return PTS_tserrInvalidParameter;output[0]={0,100};output[1]={10,invalid_columns?250:100};*actual=2;return 0;
}
static int WINAPI main_segment(void*,void*,void** segment){*segment=reinterpret_cast<void*>(77);return 0;}
static int WINAPI first_para(void*,void* segment,int* found,void** name){if(segment!=reinterpret_cast<void*>(77)&&!containers&&!subpage_parent&&!inside_table_cell)return PTS_tserrInvalidParameter;*found=1;*name=segment==reinterpret_cast<void*>(77)?reinterpret_cast<void*>(101):reinterpret_cast<void*>(201);return 0;}
static int WINAPI next_para(void*,void* segment,void* current,int* found,void** next){*found=segment==reinterpret_cast<void*>(77)&&reinterpret_cast<uintptr_t>(current)<uintptr_t(100+para_count);*next=*found?reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(current)+1):nullptr;return 0;}
static int WINAPI para_properties(void*,void* name,FSPAP* pap){*pap={};pap->idobj=table_mode&&!inside_table_cell&&!(span_prefix&&name==reinterpret_cast<void*>(101))?3:subpage_parent&&reinterpret_cast<uintptr_t>(name)<200?1:block_ui&&name==reinterpret_cast<void*>(102)?2:containers&&reinterpret_cast<uintptr_t>(name)<200?0:-1;pap->fKeepWithNext=keep_next&&name==reinterpret_cast<void*>(102);if(name==reinterpret_cast<void*>(102)){pap->fBreakPageBefore=hard_break==1;pap->fBreakColumnBefore=hard_break==2;}return 0;}
static int WINAPI para_client(void*,void* name,void** output){++clients_created;*output=name;return 0;}
static int WINAPI destroy_para_client(void*,void* client){auto at=figure_content.find(client);if(at!=figure_content.end()){void* page=at->second;figure_content.erase(at);require(destroy_subpage(page_context,page)==0,"original figure client owns its subpage through disposal");}++clients_destroyed;return 0;}
static int WINAPI text_properties(void*,void* name,int,FSTXTPROPS* props){*props={};props->fKeepTogether=keep_mode==2||(keep_mode==1&&name==reinterpret_cast<void*>(102));return 0;}
static int WINAPI line_format(void*,void*,void* name,int,int dcp,void* incoming,uint32_t direction,
    int start,int width,int track_start,int track_width,int margin,int hyphen,int left,int right,int first,int last,int suppress,
    void** line,int* consumed,void** br,int* forced,FSFLRES* result,int* ascent,int* descent,int* bbox,int* bbox_width,int* depend,int* reformat) {
    if(!inside_figure&&(direction||(!attached_float&&!attached_figure&&(start!=track_start||width!=100))||(track_start!=10&&track_start!=120)||track_width!=100||margin!=track_start||hyphen||left||right||last||first!=(dcp==0)||bool(incoming)!=(dcp!=0)))return PTS_tserrInvalidParameter;
    if(attached_float&&!inline_anchor_mode&&(!deferred_attachment||name==reinterpret_cast<void*>(102))&&(width!=(multiple_opposite?20:60)||start!=(attached_right?10:50)))return PTS_tserrInvalidParameter;
    ++lines_created;*line=reinterpret_cast<void*>(1000+lines_created);*br=reinterpret_cast<void*>(2000+lines_created);
    *consumed=5;*forced=0;*result=dcp==0?fsflrOutOfSpace:fsflrEndOfParagraph;*ascent=7;*descent=3;*bbox=start;*bbox_width=attached_figure?std::min(75,width):75;*depend=*reformat=0;
    if(inline_anchor_mode){if(start!=(width==100?10:50)||(width!=60&&width!=100))return PTS_tserrInvalidParameter;*consumed=std::min(5,10-dcp);if(inline_anchor_mode==2&&dcp==0&&width==60)*consumed=2;*result=dcp+*consumed==10?fsflrEndOfParagraph:fsflrOutOfSpace;}
    if(split_table_cells&&short_table_cell&&dcp==0)*result=fsflrEndOfParagraph;
    if(long_table_cell)*result=dcp+5>=45?fsflrEndOfParagraph:fsflrOutOfSpace;
    if(structural_line&&dcp==0){*ascent=*descent=*bbox_width=0;}
    if(lines_created==fail_line)return PTS_tserrCallbackException;
    return 0;
}
static int WINAPI destroy_line(void*,void*){++lines_destroyed;return 0;}
static int WINAPI destroy_break(void*,void*){++breaks_destroyed;return 0;}
static int WINAPI destroy_margin(void*,void*){return 0;}
static int WINAPI block_props(void*,void* name,uint32_t,FSFLOATERPROPS* output){*output={};output->fFloat=float_obstacle||attached_float;output->fskwr=inline_anchor_mode==6&&name==reinterpret_cast<void*>(302)?fskwrNone:attached_float?fskwrBoth:fskwrNone;output->fskfloatalignment=(attached_right||((multiple_opposite||inline_anchor_mode==6)&&name==reinterpret_cast<void*>(302)))?fskfloatalignMax:fskfloatalignMin;return 0;}
static int WINAPI block_margin(void*,void*,void*,uint32_t,void*,int,int* output){*output=0;return 0;}
static int WINAPI block_after(void*,void*,void*,uint32_t,void*,void** output){*output=nullptr;return 0;}
static int WINAPI block_destroy(void*){++float_destroyed;return 0;}
static int WINAPI block_destroy_break(void*,void* record){if(paginate_float){if(!float_live_records.erase(record))return PTS_tserrInvalidBreakRecord;delete static_cast<MockFloaterRecord*>(record);}++float_records_destroyed;return 0;}
static int WINAPI block_finite(void*,void* para,void* input,int previous,void*,void*,int empty,int,uint32_t,int,int width,int height,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA,
    FSFMTR* formatted,void** content,void** br,int* used_width,int* used_height,FSBBOX* box,int* polygons,int* vertices) {
    *formatted={};*content=*br=nullptr;*used_width=*used_height=*polygons=*vertices=0;*box={};
    if(height<25&&empty){*used_width=40;*used_height=25;
        if(split_attachment){++float_created;*content=para;*br=reinterpret_cast<void*>(4001);formatted->kstop=fmtrBrokenOutOfSpace;}
        else formatted->kstop=fmtrNoProgressOutOfSpace;return 0;}
    ++float_created;*content=para;*used_width=attached_float?40:width;*used_height=25;*box={1,{0,0,*used_width,25}};formatted->fForcedProgress=height<25;
    if(paginate_float){
        if(input&&!float_live_records.count(input))return PTS_tserrInvalidBreakRecord;
        if(bool(input)!=bool(previous))return PTS_tserrInvalidParameter;
        int stage=input?static_cast<MockFloaterRecord*>(input)->stage:0;
        if(stage<2){auto record=new MockFloaterRecord{stage+1};float_live_records.insert(record);++float_records_created;*br=record;formatted->kstop=fmtrBrokenOutOfSpace;}
    }
    return (fail_float||float_created==float_fail_at)?PTS_tserrCallbackException:0;
}
static int WINAPI table_props(void*,void*,uint32_t direction,FSTABLEOBJPROPS* props){*props={};props->fswdirTable=direction;return 0;}
static int WINAPI table_fit(void*,void*,void*,uint32_t,int width,int* output){*output=width;return 0;}
static int WINAPI table_first(void*,void*,int* found,void** name){*found=table_rows_count>0;*name=*found?reinterpret_cast<void*>(501):nullptr;return 0;}
static int WINAPI table_next(void*,void*,void* current,int* found,void** name){auto n=reinterpret_cast<uintptr_t>(current);*found=n<uintptr_t(500+table_rows_count);*name=*found?reinterpret_cast<void*>(n+1):nullptr;return 0;}
static int WINAPI table_row_props(void*,void*,uint32_t,FSTABLEROWPROPS* props){*props={};props->cCells=2;props->dvrAboveRow=props->dvrBelowRow=props->dvrAboveTopRow=props->dvrBelowBottomRow=props->dvrAboveRowBreak=props->dvrBelowRowBreak=1;return 0;}
static int WINAPI table_get_cells(void*,void* row,int count,void** names,FSTABLEKCELLMERGE* merge){if(count!=2)return PTS_tserrInvalidParameter;auto n=reinterpret_cast<uintptr_t>(row);for(int i=0;i<2;++i){names[i]=reinterpret_cast<void*>(table_spans?(i==0?602+n-501:601):601+(n-501)*2+i);merge[i]=table_spans&&i==1?(n==501?(malformed_span?fskcellmergeMiddle:fskcellmergeFirst):n==503?fskcellmergeLast:fskcellmergeMiddle):fskcellmergeNo;}return 0;}
static int WINAPI table_cell_format(void*,void*,void* input,void* name,void*,int empty,uint32_t direction,int extra,int available,FSFMTR* formatted,void** output,void** br,int* height){
    *formatted={};*output=*br=nullptr;*height=0;if((input&&!split_table_cells&&!long_span)||extra)return PTS_tserrInvalidParameter;if(empty&&available<20){formatted->kstop=fmtrNoProgressOutOfSpace;return 0;}
    FSRECT rect{10,20,100,available};FSCOLUMNINFO column{0,100};FSBBOX bbox{};void* margin=nullptr;int top=0;void* page=nullptr;
    inside_table_cell=true;short_table_cell=split_table_cells&&name==reinterpret_cast<void*>(601);long_table_cell=long_span&&name==reinterpret_cast<void*>(601);int status=create_subpage(page_context,input,input?1:0,name,nullptr,empty,1,direction,200,20+available,&rect,1,&column,0,0,nullptr,nullptr,0,nullptr,nullptr,0,fsksuppresshardbreakbeforefirstparaNone,formatted,&page,br,height,&bbox,&margin,&top);inside_table_cell=false;short_table_cell=false;long_table_cell=false;
    if(*br)++table_records_created;
    if(status)return status;if(margin)return PTS_tserrInvalidClientOutput;
    auto cell=new MockTableCell{page};++table_cells_created;*output=cell;
    if(lose_table_cell){delete cell;++table_cells_destroyed;*output=*br=nullptr;return PTS_tserrCallbackException;}
    return table_cells_created==table_fail_cell?PTS_tserrCallbackException:0;
}
static int WINAPI table_cell_bottomless(void* client,void* table,void* name,uint32_t direction,FSFMTRBL* formatted,void** output,int* height){FSFMTR finite{};void* br=nullptr;int status=table_cell_format(client,table,nullptr,name,nullptr,0,direction,0,long_span?200:80,&finite,output,&br,height);*formatted=fmtrblGoalReached;if(br)return PTS_tserrInvalidClientOutput;if(!status&&name==reinterpret_cast<void*>(601))*height=long_span?150:45;return status;}
static int WINAPI table_destroy_cell(void* handle){auto cell=static_cast<MockTableCell*>(handle);int status=destroy_subpage(page_context,cell->page);delete cell;++table_cells_destroyed;return status;}
static int WINAPI table_set_height(void*,void*,void*,void* name,int,uint32_t,int height){if(table_spans){if(name==reinterpret_cast<void*>(601))span_set_height=height;if(long_span)return height>=0?0:PTS_tserrInvalidClientOutput;return height==(name==reinterpret_cast<void*>(601)?64:20)?0:PTS_tserrInvalidClientOutput;}return (split_table_cells?height>=0&&height<=20:height==20)?0:PTS_tserrInvalidClientOutput;}
static int WINAPI table_clear_cell(void* handle){return clear_cell_subpage(page_context,static_cast<MockTableCell*>(handle)->page);}
static int WINAPI table_destroy_record(void*,void* record){int status=destroy_subpage_record(page_context,record);if(!status)++table_records_destroyed;return status;}
static int WINAPI table_object_handler(void* output){FSTABLEOBJINIT init{};init.tableobjcbk.pfnGetTableProperties=reinterpret_cast<void*>(table_props);init.tableobjcbk.pfnAutofitTable=reinterpret_cast<void*>(table_fit);init.tableobjcbk.pfnGetMCSClientAfterTable=reinterpret_cast<void*>(block_after);auto& fetch=init.tablecbkfetch;fetch.pfnGetFirstRow=reinterpret_cast<void*>(table_first);fetch.pfnGetNextRow=reinterpret_cast<void*>(table_next);fetch.pfnGetRowProperties=reinterpret_cast<void*>(table_row_props);fetch.pfnGetCells=reinterpret_cast<void*>(table_get_cells);auto& cells=init.tablecbkcell;cells.pfnFormatCellFinite=reinterpret_cast<void*>(table_cell_format);cells.pfnFormatCellBottomless=reinterpret_cast<void*>(table_cell_bottomless);cells.pfnDestroyCell=reinterpret_cast<void*>(table_destroy_cell);cells.pfnDestroyCellBreakRecord=reinterpret_cast<void*>(table_destroy_record);cells.pfnSetCellHeight=reinterpret_cast<void*>(table_set_height);cells.pfnClearUpdateInfoInCell=reinterpret_cast<void*>(table_clear_cell);return table_handler(&init,output);}
static int WINAPI block_handler(void*,int id,void* output) {
    if(id==3&&table_mode)return table_object_handler(output);
    if(id!=2)return PTS_tserrInvalidParameter;
    FSFLOATERINIT init{};auto& cb=init.fsfloatercbk;cb.pfnGetFloaterProperties=reinterpret_cast<void*>(block_props);
    cb.pfnFormatFloaterContentFinite=reinterpret_cast<void*>(block_finite);cb.pfnDestroyFloaterContent=reinterpret_cast<void*>(block_destroy);
    cb.pfnDestroyFloaterContentBreakRecord=reinterpret_cast<void*>(block_destroy_break);cb.pfnGetMCSClientAfterFloater=reinterpret_cast<void*>(block_after);
    return floater_handler(&init,output);
}
static int WINAPI attached_count(void*,void* name,int dcp,int* count){*count=(attached_float||attached_figure)&&(!inline_anchor_mode||inline_anchor_mode==6)&&name==reinterpret_cast<void*>(deferred_attachment?102:101)&&dcp==0?(multiple_float?2:1):0;return 0;}
static int WINAPI attached_objects(void*,void*,int,int capacity,void** names,int* kinds,int* anchors,int* actual,int* ended){if(capacity!=(multiple_float?2:1))return PTS_tserrInvalidParameter;for(int index=0;index<capacity;++index){names[index]=reinterpret_cast<void*>((inline_anchor_mode==6?302:301)+index);kinds[index]=attached_figure?-2:2;anchors[index]=index;}*actual=capacity;*ended=0;return 0;}
static int WINAPI attached_in_line(void*,void*,void*,int first,int limit,int,int,int* count){int anchor=inline_anchor_mode==2?4:2;*count=inline_anchor_mode&&first<=anchor&&anchor<limit?1:0;if(inline_anchor_mode==3)*count=-1;return 0;}
static int WINAPI inline_objects(void*,void*,void*,int,int,int,int,int capacity,void** names,int* kinds,int* anchors,int* actual){if(capacity!=1)return PTS_tserrInvalidParameter;names[0]=reinterpret_cast<void*>(301);kinds[0]=2;anchors[0]=inline_anchor_mode==2?4:2;*actual=inline_anchor_mode==4?0:1;return inline_anchor_mode==5?PTS_tserrCallbackException:0;}
static int WINAPI figure_properties(void*,void* client,void* name,int in_line,uint32_t direction,int bottomless,int* width,int* height,FSFIGUREPROPS* props,int* polygons,int* vertices,int* left,int* right,int* top,int* bottom) {
    *props={};props->fskrefU=props->fskrefV=fskrefParagraph;props->fskalfU=static_cast<FSKALIGNFIG>(figure_alignment);props->fskwrap=fskwrBoth;props->fDelayable=1;
    *width=40;*height=25;*polygons=*vertices=*left=*right=*top=*bottom=0;
    if(name!=reinterpret_cast<void*>(301)||!in_line||bottomless)return PTS_tserrInvalidParameter;
    FSRECT rect{0,0,40,80};FSCOLUMNINFO column{0,40};FSFMTR formatted{};FSBBOX box{};void* page=nullptr;void* br=nullptr;void* margin=nullptr;int used=0,space=0;
    inside_table_cell=inside_figure=true;int status=create_subpage(page_context,nullptr,0,name,nullptr,0,1,direction,40,80,&rect,1,&column,0,0,nullptr,nullptr,0,nullptr,nullptr,0,fsksuppresshardbreakbeforefirstparaNone,&formatted,&page,&br,&used,&box,&margin,&space);inside_table_cell=inside_figure=false;
    if(status)return status;
    require(!br&&!margin&&used==20,"figure properties produce an original complete finite subpage");
    if(figure_fail_properties)return PTS_tserrCallbackException;
    figure_content[client]=page;return 0;
}
static int WINAPI figure_position(void*,void*,void*,uint32_t,FSRECT* page,FSRECT* margin,FSRECT* track,FSRECT* preliminary,int,int,int* push,FSRECT* flow,FSRECT* overlap,FSBBOX* box,FSRECT* search) {
    require(page->du==200&&page->dv==140&&margin->u==10&&margin->v==20&&track->du==100,"figure position receives actual source page/margin/track rectangles");
    *push=0;*flow=*overlap=*preliminary;*box={1,*flow};*search=*track;
    return figure_fail_position?PTS_tserrCallbackException:0;
}
static void reset_page_counts(){lines_created=lines_destroyed=breaks_destroyed=clients_created=clients_destroyed=0;}

int main(int argc,char** argv) {
    try {
        require(argc==2,"usage: check DLL");auto dll=LoadLibraryA(argv[1]);require(dll!=nullptr,"native DLL load");
        using Install=int(WINAPI*)(FSIMETHODS*,FSIMETHODS*,void**,int*);using Create=int(WINAPI*)(FSCONTEXTINFO*,void**);using Destroy=int(WINAPI*)(void*);using State=int(WINAPI*)(void*,FSCONTEXTINFO*,FSIMETHODS*,void**);
#define PROC(name,type) auto name=reinterpret_cast<type>(GetProcAddress(dll,#name));require(name!=nullptr,#name)
        PROC(CreateInstalledObjectsInfo,Install);PROC(DestroyInstalledObjectsInfo,Destroy);PROC(CreateDocContext,Create);PROC(DestroyDocContext,Destroy);PROC(WpfGetParagraphContextState,State);
        using Transform=int(WINAPI*)(uint32_t,const FSRECT*,const FSRECT*,uint32_t,FSRECT*);
        using TransformBox=int(WINAPI*)(uint32_t,const FSRECT*,const FSBBOX*,uint32_t,FSBBOX*);
        PROC(FsTransformRectangle,Transform);PROC(FsTransformBbox,TransformBox);
        auto equal=[](FSRECT a,FSRECT b){return a.u==b.u&&a.v==b.v&&a.du==b.du&&a.dv==b.dv;};
        FSRECT page{17,29,900,600},physical{72,103,83,47};
        // Independent corner projection: construct the axes and origin named
        // by ES/EN/SE/SW/WS/WN/NE/NW, then project physical corner points.
        const int ux[]={1,1,0,0,-1,-1,0,0},uy[]={0,0,1,1,0,0,-1,-1};
        const int vx[]={0,0,1,-1,0,0,1,-1},vy[]={1,-1,0,0,1,-1,0,0};
        auto project=[&](int direction,FSRECT r) {
            int originX=(ux[direction]<0||vx[direction]<0)?page.du:0;
            int originY=(uy[direction]<0||vy[direction]<0)?page.dv:0;
            int loU=INT_MAX,loV=INT_MAX,hiU=INT_MIN,hiV=INT_MIN;
            for(int dx:{0,r.du})for(int dy:{0,r.dv}) {
                int x=r.u+dx-page.u-originX,y=r.v+dy-page.v-originY;
                int u=x*ux[direction]+y*uy[direction],v=x*vx[direction]+y*vy[direction];
                loU=std::min(loU,u);hiU=std::max(hiU,u);loV=std::min(loV,v);hiV=std::max(hiV,v);
            }
            return FSRECT{page.u+loU,page.v+loV,hiU-loU,hiV-loV};
        };
        for(uint32_t in=0;in<8;++in)for(uint32_t out=0;out<8;++out) {
            FSRECT src=project(in,physical),expected=project(out,physical),actual{};
            require(FsTransformRectangle(in,&page,&src,out,&actual)==0&&equal(actual,expected),"all writing directions agree with independent physical corner projection");
            require(FsTransformRectangle(in,&page,&src,out,&src)==0&&equal(src,expected),"rectangle ref/out aliasing");
            FSBBOX box{1,project(in,physical)};
            require(FsTransformBbox(in,&page,&box,out,&box)==0&&box.fDefined==1&&equal(box.fsrc,expected),"defined bbox geometry and aliasing");
        }
        FSRECT outside{-100,-50,0,20},mirrored{};
        require(FsTransformRectangle(0,&page,&outside,4,&mirrored)==0&&mirrored.u==1034&&mirrored.v==-50&&mirrored.du==0,"outside-page and zero-width rectangles are retained");
        FSBBOX undefined{0,{INT_MIN,INT_MAX,-1,-1}},empty{};
        require(FsTransformBbox(0,&page,&undefined,4,&empty)==0&&!empty.fDefined&&equal(empty.fsrc,FSRECT{}),"undefined bbox does not inspect meaningless coordinates");
        FSRECT overflow{INT_MIN,0,1,1},invalid{1,2,3,4};
        require(FsTransformRectangle(0,&page,&overflow,4,&invalid)==PTS_tserrInvalidParameter&&equal(invalid,FSRECT{}),"overflow rejects without narrowing or partial output");
        require(FsTransformRectangle(8,&page,&physical,0,&invalid)==PTS_tserrInvalidParameter&&equal(invalid,FSRECT{}),"invalid direction rejects and clears output");
        require(FsTransformRectangle(0,nullptr,&physical,0,&invalid)==PTS_tserrInvalidParameter&&equal(invalid,FSRECT{}),"null geometry rejects");
        require(FsTransformRectangle(0,&page,&physical,0,nullptr)==PTS_tserrNullOutputParameter,"null output rejects");
        FSRECT negative{0,0,-1,1};require(FsTransformRectangle(0,&page,&negative,0,&invalid)==PTS_tserrInvalidParameter,"negative extent rejects");
        std::puts("paragraph geometry: 64 direction pairs, corner projection, aliasing, empty/outside, overflow and invalid arguments PASS");
        FSIMETHODS methods{};methods.pfnCreateContext=reinterpret_cast<void*>(create_object);methods.pfnDestroyContext=reinterpret_cast<void*>(destroy_object);methods.pfnFormatParaFinite=reinterpret_cast<void*>(1234);
        void* installed=nullptr;int count=0;
        require(CreateInstalledObjectsInfo(&methods,&methods,&installed,&count)==0&&installed&&count==4,"source installed object registration");
        FSCONTEXTINFO info{};info.fsffi=0x140;info.drMinColumnBalancingStep=3000;info.cInstalledObjects=count;info.pInstalledObjects=installed;info.pfsclient=reinterpret_cast<void*>(55);
        methods.pfnCreateContext=nullptr;methods.pfnFormatParaFinite=nullptr; // caller mutations must not alter retained tables
        void* context=nullptr;require(CreateDocContext(&info,&context)==0&&context&&creates==2,"copied object callbacks create both owned contexts");
        FSCONTEXTINFO actual{};FSIMETHODS copied[2]{};void* contexts[2]{};
        require(WpfGetParagraphContextState(context,&actual,copied,contexts)==0&&actual.fsffi==0x140&&actual.pfsclient==reinterpret_cast<void*>(55)&&copied[0].pfnFormatParaFinite==reinterpret_cast<void*>(1234)&&contexts[0]==reinterpret_cast<void*>(100)&&contexts[1]==reinterpret_cast<void*>(101),"retained context ABI/configuration/object identity snapshot");
        require(DestroyInstalledObjectsInfo(installed)==0&&DestroyInstalledObjectsInfo(installed)==PTS_tserrInvalidParameter,"installed registration rejects double destruction");
        require(WpfGetParagraphContextState(context,&actual,copied,contexts)==0,"document independently retains installed callback ownership");
        require(DestroyDocContext(context)==0&&destroys==2&&DestroyDocContext(context)==PTS_tserrInvalidLsContext,"document teardown calls source destructors once");
        require(WpfGetParagraphContextState(context,&actual,copied,contexts)==PTS_tserrInvalidLsContext&&!contexts[0]&&!actual.pfsclient,"destroyed state queries are empty and reject without dereferencing");
        context=reinterpret_cast<void*>(1);require(CreateDocContext(&info,&context)==PTS_tserrInvalidParameter&&!context,"destroyed installed registration cannot create a new document");
        methods.pfnCreateContext=reinterpret_cast<void*>(create_object);require(CreateInstalledObjectsInfo(&methods,&methods,&installed,&count)==0,"rollback registration");info.pInstalledObjects=installed;
        fail_id=1;creates=destroys=0;context=reinterpret_cast<void*>(1);
        require(CreateDocContext(&info,&context)==PTS_tserrCallbackException&&!context&&creates==2&&destroys==1,"second object creation failure rolls back the first object and clears output");
        fail_id=-1;info.version=1;require(CreateDocContext(&info,&context)==PTS_tserrInvalidParameter&&!context,"unsupported version rejects without fabricated context");
        require(DestroyInstalledObjectsInfo(installed)==0,"rollback registration disposal");
        using PageCreate=int(WINAPI*)(void*,void*,void*,FSFMTR*,void**,void**);
        using QueryPage=int(WINAPI*)(void*,void*,FSPAGEDETAILS*);
        using QueryTrack=int(WINAPI*)(void*,void*,FSTRACKDETAILS*);
        using QueryParas=int(WINAPI*)(void*,void*,int,FSPARADESCRIPTION*,int*);
        using QueryText=int(WINAPI*)(void*,void*,FSTEXTDETAILS*);
        using QueryLines=int(WINAPI*)(void*,void*,int,FSLINEDESCRIPTIONSINGLE*,int*);
        using PageAction=int(WINAPI*)(void*,void*);
        PROC(FsCreatePageFinite,PageCreate);PROC(FsDestroyPage,PageAction);PROC(FsDestroySubtrack,PageAction);
        PROC(FsQueryPageDetails,QueryPage);PROC(FsQueryTrackDetails,QueryTrack);PROC(FsQueryTrackParaList,QueryParas);
        PROC(FsQuerySubtrackParaList,QueryParas);
        PROC(FsQueryTextDetails,QueryText);PROC(FsQueryLineListSingle,QueryLines);PROC(FsClearUpdateInfoInPage,PageAction);
        format_subtrack=reinterpret_cast<SubtrackFormat>(GetProcAddress(dll,"FsFormatSubtrackFinite"));destroy_subtrack=FsDestroySubtrack;
        require(format_subtrack!=nullptr,"finite subtrack export");
        destroy_subtrack_record=reinterpret_cast<decltype(destroy_subtrack_record)>(GetProcAddress(dll,"FsDestroySubtrackBreakRecord"));require(destroy_subtrack_record!=nullptr,"subtrack record destruction export");
        methods.pfnDestroyBreakRecord=reinterpret_cast<void*>(container_destroy_record);
        methods.pfnFormatParaFinite=reinterpret_cast<void*>(container_format);methods.pfnDestroyPara=reinterpret_cast<void*>(container_destroy);
        create_subpage=reinterpret_cast<NativeSubpageFinite>(GetProcAddress(dll,"FsCreateSubpageFinite"));destroy_subpage=reinterpret_cast<decltype(destroy_subpage)>(GetProcAddress(dll,"FsDestroySubpage"));destroy_subpage_record=reinterpret_cast<decltype(destroy_subpage_record)>(GetProcAddress(dll,"FsDestroySubpageBreakRecord"));require(create_subpage&&destroy_subpage&&destroy_subpage_record,"subpage object exports");
        FSIMETHODS subpage_methods=methods;subpage_methods.pfnFormatParaFinite=reinterpret_cast<void*>(subpage_format);subpage_methods.pfnDestroyPara=reinterpret_cast<void*>(subpage_destroy);subpage_methods.pfnDestroyBreakRecord=reinterpret_cast<void*>(subpage_destroy_record);
        require(CreateInstalledObjectsInfo(&methods,&subpage_methods,&installed,&count)==0,"page installed registration");
        info.version=0;info.pInstalledObjects=installed;
        auto& gen=info.fscbk.cbkgen;auto& txt=info.fscbk.cbktxt;
        gen.pfnGetPageDimensions=reinterpret_cast<void*>(page_geometry);gen.pfnGetSectionProperties=reinterpret_cast<void*>(section_properties);gen.pfnGetMainTextSegment=reinterpret_cast<void*>(main_segment);gen.pfnGetSectionColumnInfo=reinterpret_cast<void*>(column_info);
        gen.pfnGetFirstPara=reinterpret_cast<void*>(first_para);gen.pfnGetNextPara=reinterpret_cast<void*>(next_para);gen.pfnGetParaProperties=reinterpret_cast<void*>(para_properties);
        gen.pfnCreateParaclient=reinterpret_cast<void*>(para_client);gen.pfnDestroyParaclient=reinterpret_cast<void*>(destroy_para_client);
        txt.pfnGetTextProperties=reinterpret_cast<void*>(text_properties);txt.pfnFormatLine=reinterpret_cast<void*>(line_format);
        txt.pfnGetNumberAttachedObjectsBeforeTextLine=reinterpret_cast<void*>(attached_count);txt.pfnGetAttachedObjectsBeforeTextLine=reinterpret_cast<void*>(attached_objects);txt.pfnGetNumberAttachedObjectsInTextLine=reinterpret_cast<void*>(attached_in_line);txt.pfnGetAttachedObjectsInTextLine=reinterpret_cast<void*>(inline_objects);
        info.fscbk.cbkfig.pfnGetFigureProperties=reinterpret_cast<void*>(figure_properties);info.fscbk.cbkfig.pfnCalcFigurePosition=reinterpret_cast<void*>(figure_position);
        txt.pfnDestroyLine=reinterpret_cast<void*>(destroy_line);txt.pfnDestroyLineBreakRecord=reinterpret_cast<void*>(destroy_break);
        info.fscbk.cbkobj.pfnDestroyMcsclient=reinterpret_cast<void*>(destroy_margin);
        floater_handler=reinterpret_cast<decltype(floater_handler)>(GetProcAddress(dll,"GetFloaterHandlerInfo"));require(floater_handler!=nullptr,"floater handler registration export");
        info.fscbk.cbkobj.pfnGetObjectHandlerInfo=reinterpret_cast<void*>(block_handler);
        info.fscbk.cbkobj.pfnConvertMcsclient=reinterpret_cast<void*>(block_margin);
        require(CreateDocContext(&info,&context)==0,"page context creation");page_context=context;reset_page_counts();
        FSFMTR formatted{};void* page_handle=nullptr;void* page_break=nullptr;
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_handle&&!page_break&&formatted.kstop==fmtrGoalReached,"finite page creates actual callback paragraphs and lines");
        require(lines_created==4&&clients_created==2&&!lines_destroyed&&!breaks_destroyed&&!clients_destroyed,"page retains line/client/break handles until release");
        require(DestroyDocContext(context)==PTS_tserrInvalidPtsContext,"context cannot be destroyed with live pages");
        FSPAGEDETAILS details{};require(FsQueryPageDetails(context,page_handle,&details)==0&&details.fSimple&&details.fskupd==fskupdNew&&details.u.simple.trackdescr.fsrc.u==10&&details.u.simple.trackdescr.fsrc.v==20,"original callback page margins and update state");
        void* track=details.u.simple.trackdescr.pfstrack;FSTRACKDETAILS track_details{};
        require(FsQueryTrackDetails(context,track,&track_details)==0&&track_details.cParas==2,"track actual paragraph count");
        FSPARADESCRIPTION paras[2]{};int actual_count=0;
        require(FsQueryTrackParaList(context,track,2,paras,&actual_count)==0&&actual_count==2&&paras[0].dvrUsed==20&&paras[1].dvrUsed==20&&paras[0].fsbbox.fsrc.u==10&&paras[1].fsbbox.fsrc.v==40,"paragraph retained geometry and identities");
        require(FsQueryTrackParaList(context,track,1,paras,&actual_count)==PTS_tserrInvalidParameter&&!actual_count,"short paragraph array rejects without partial writes");
        FSTEXTDETAILS text_details{};
        require(FsQueryTextDetails(context,paras[0].pfspara,&text_details)==0&&text_details.fsktd==fsktdFull&&text_details.u.full.cLines==2&&text_details.u.full.dcpFirst==0&&text_details.u.full.dcpLim==10,"text details describe actual formatted source range");
        FSLINEDESCRIPTIONSINGLE lines[2]{};
        require(FsQueryLineListSingle(context,paras[0].pfspara,2,lines,&actual_count)==0&&actual_count==2&&lines[0].dcpFirst==0&&lines[0].dcpLim==5&&lines[1].dcpFirst==5&&lines[1].dcpLim==10&&lines[0].urStart==10&&lines[0].vrStart==20&&lines[1].vrStart==30&&lines[1].pfsbreakreclineclient==reinterpret_cast<void*>(2001),"line metrics, ranges and continuation record");
        require(FsQueryPageDetails(reinterpret_cast<void*>(1),page_handle,&details)==PTS_tserrInvalidQuery&&!details.fSimple,"foreign context cannot query a page");
        require(FsDestroySubtrack(context,track)==PTS_tserrInvalidQuery,"page-owned root track cannot be destroyed as a subtrack");
        require(FsClearUpdateInfoInPage(context,page_handle)==0&&FsQueryPageDetails(context,page_handle,&details)==0&&details.fskupd==fskupdNoChange,"page update clear is retained");
        require(FsDestroyPage(context,page_handle)==0&&lines_destroyed==4&&breaks_destroyed==4&&clients_destroyed==2&&FsDestroyPage(context,page_handle)==PTS_tserrInvalidQuery,"page teardown releases every callback handle exactly once");
        require(FsQueryTextDetails(context,paras[0].pfspara,&text_details)==PTS_tserrInvalidQuery&&FsQueryTrackDetails(context,track,&track_details)==PTS_tserrInvalidQuery,"page teardown invalidates child queries");
        containers=true;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_handle&&clients_created==4&&lines_created==4,"container callbacks recursively format real subtracks");
        require(FsDestroyPage(context,page_handle)==0&&clients_destroyed==4&&lines_destroyed==4&&breaks_destroyed==4,"recursive container teardown releases child tracks and callback handles");
        reset_page_counts();lose_subtrack=true;
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"object callback failure after successful subtrack export rolls back a reset out handle");
        available_height=10;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&!page_break&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"object callback failure resets both partial subtrack and continuation outputs without leaking either");
        available_height=80;containers=false;lose_subtrack=false;
        reset_page_counts();fail_line=2;page_handle=reinterpret_cast<void*>(1);page_break=reinterpret_cast<void*>(1);
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&!page_break&&lines_created==lines_destroyed&&lines_created==breaks_destroyed&&clients_created==clients_destroyed,"partial callback allocation failure rolls back line/client/break ownership");
        fail_line=-1;available_height=10;reset_page_counts();
        PROC(FsDestroyPageBreakRecord,PageAction);
        void* incoming_record=nullptr;keep_mode=2;
        for(int number=0;number<4;++number) {
            require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_handle,"finite continuation formats the next real page");
            if(incoming_record)require(FsDestroyPageBreakRecord(context,incoming_record)==0,"input record can be released after continuation");
            require(FsQueryPageDetails(context,page_handle,&details)==0,"paginated page details");
            require(FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1,"one accepted text line/paragraph per short page");
            require(paras[0].nmp==reinterpret_cast<void*>(101+number/2)&&FsQueryTextDetails(context,paras[0].pfspara,&text_details)==0&&text_details.u.full.dcpFirst==(number%2)*5&&text_details.u.full.dcpLim==(number%2)*5+5,"four pages cover both source paragraphs exactly once without gaps/overlap");
            require((number<3)==bool(page_break)&&formatted.kstop==(number<3?fmtrBrokenOutOfSpace:fmtrGoalReached),"page result and continuation identity agree with source exhaustion");
            require(FsDestroyPage(context,page_handle)==0,"page can be disposed while its independent continuation remains");incoming_record=page_break;
        }
        require(!incoming_record&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"pagination releases speculative and accepted lines and independent records exactly once");
        keep_mode=0;
        std::puts("native finite pagination: four-page exact source coverage, real break records, input/page-independent lifetime and complete callback cleanup PASS");
        for(bool nested:{false,true})for(int height:{4,0}) {
        containers=nested;available_height=height;reset_page_counts();incoming_record=nullptr;
        for(int number=0;number<4;++number) {
            require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_handle&&formatted.fForcedProgress,"undersized page accepts one source line and reports forced progress");
            if(incoming_record)require(FsDestroyPageBreakRecord(context,incoming_record)==0,"forced input record release");
            require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].dvrUsed==height,"forced layout accounts for available vertical extent without dropping its client line");
            void* text_handle=paras[0].pfspara;
            if(nested)require(FsQuerySubtrackParaList(context,text_handle,2,paras,&actual_count)==0&&actual_count==1,"forced container retains its real text child"),text_handle=paras[0].pfspara;
            require(FsQueryTextDetails(context,text_handle,&text_details)==0&&text_details.u.full.cLines==1&&text_details.u.full.dcpFirst==(number%2)*5&&text_details.u.full.dcpLim==(number%2)*5+5,"forced pages advance actual character ranges exactly once");
            require((number<3)==bool(page_break),"last oversized line does not fabricate another continuation");
            require(FsDestroyPage(context,page_handle)==0,"forced page disposal");incoming_record=page_break;
        }
        require(!incoming_record&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"forced and speculative callback resources tear down once");
        }
        containers=false;
        std::puts("native forced progress: undersized/zero-height flat/nested pages, real accepted line/ranges, available extent, terminal continuation and exact cleanup PASS");
        available_height=30;keep_mode=1;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_break,"keep-together paragraph is deferred rather than partially consumed");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].nmp==reinterpret_cast<void*>(101),"first page retains preceding text and no fragment of the kept paragraph");
        require(FsDestroyPage(context,page_handle)==0,"keep-together first page disposal");incoming_record=page_break;
        require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break&&FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].nmp==reinterpret_cast<void*>(102)&&FsQueryTextDetails(context,paras[0].pfspara,&text_details)==0&&text_details.u.full.dcpFirst==0&&text_details.u.full.dcpLim==10,"kept paragraph resumes intact from its first character on the next page");
        require(FsDestroyPageBreakRecord(context,incoming_record)==0&&FsDestroyPage(context,page_handle)==0&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"kept and speculative line/break/client resources are released exactly once");
        keep_mode=0;available_height=10;
        std::puts("native keep-together: intact deferral/resume, preceding source preservation, oversized kept paragraph progress and exact teardown PASS");
        para_count=3;keep_next=1;available_height=40;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_break,"keep-with-next chain rolls back an already accepted heading when following text cannot fit");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].nmp==reinterpret_cast<void*>(101),"heading is removed from the preceding page without dropping prefix content");
        require(FsDestroyPage(context,page_handle)==0,"keep-chain first page release");incoming_record=page_break;
        require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break&&FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==2&&paras[0].nmp==reinterpret_cast<void*>(102)&&paras[1].nmp==reinterpret_cast<void*>(103),"heading and following paragraph resume together from chain start");
        require(FsDestroyPageBreakRecord(context,incoming_record)==0&&FsDestroyPage(context,page_handle)==0&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"accepted, rolled-back and resumed keep-chain handles are released exactly once");
        keep_next=0;para_count=2;available_height=10;
        std::puts("native keep-with-next: accepted heading rollback, intact chain resume, preceding content preservation and exact callback cleanup PASS");
        using CloneRecord=int(WINAPI*)(void*,void*,void**);
        PROC(FsDuplicateSubtrackBreakRecord,CloneRecord);reset_page_counts();
        FSRECT small{10,20,100,10};void* subtrack=nullptr;void* subrecord=nullptr;void* clone=nullptr;void* margin=nullptr;int used=0,top_space=0;FSBBOX bbox{};FSKCLEAR clear{};
        require(format_subtrack(context,nullptr,0,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&small,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==0&&subrecord&&used==10,"standalone finite subtrack exports a continuation");
        require(FsDuplicateSubtrackBreakRecord(context,subrecord,&clone)==0&&clone&&clone!=subrecord,"subtrack clone has independent wrapper identity");
        require(destroy_subtrack_record(context,subrecord)==0&&destroy_subtrack(context,subtrack)==0,"original subtrack and record can be released before their clone");
        require(DestroyDocContext(context)==PTS_tserrInvalidPtsContext&&FsDestroyPageBreakRecord(context,clone)==PTS_tserrInvalidBreakRecord,"clone retains context and rejects the wrong record destruction kind");
        require(format_subtrack(context,clone,1,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&small,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==0&&used==10,"clone retains original line-break state and resumes after page/subtrack destruction");
        require(destroy_subtrack_record(context,clone)==0&&destroy_subtrack_record(context,clone)==PTS_tserrInvalidBreakRecord&&destroy_subtrack(context,subtrack)==0&&destroy_subtrack_record(context,subrecord)==0,"cloned and continued records tear down independently and reject double release");
        require(lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"subtrack cloning releases every callback handle exactly once");
        clone=reinterpret_cast<void*>(1);require(FsDuplicateSubtrackBreakRecord(context,reinterpret_cast<void*>(1),&clone)==PTS_tserrInvalidBreakRecord&&!clone,"invalid clone clears output without dereferencing");
        std::puts("native continuation cloning: independent identities, retained resume state/context, wrong/stale records, reset-output rollback and exact teardown PASS");
        available_height=20;columns=2;reset_page_counts();
        using QuerySections=int(WINAPI*)(void*,void*,int,FSSECTIONDESCRIPTION*,int*);
        using QuerySection=int(WINAPI*)(void*,void*,FSSECTIONDETAILS*);
        using QueryColumns=int(WINAPI*)(void*,void*,int,FSTRACKDESCRIPTION*,int*);
        PROC(FsQueryPageSectionList,QuerySections);PROC(FsQuerySectionDetails,QuerySection);PROC(FsQuerySectionBasicColumnList,QueryColumns);
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_handle&&!page_break&&formatted.kstop==fmtrGoalReached,"two-column page fills both original source tracks");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&!details.fSimple&&details.u.complex.cSections==1,"column page reports complex section hierarchy");
        FSSECTIONDESCRIPTION section_desc{};FSSECTIONDETAILS section_details{};FSTRACKDESCRIPTION column_desc[2]{};
        require(FsQueryPageSectionList(context,page_handle,1,&section_desc,&actual_count)==0&&actual_count==1&&FsQuerySectionDetails(context,section_desc.pfssection,&section_details)==0&&section_details.fFootnotesAsPagenotes&&section_details.u.withpagenotes.cBasicColumns==2,"section query reports owned actual columns");
        require(FsQuerySectionBasicColumnList(context,section_desc.pfssection,2,column_desc,&actual_count)==0&&actual_count==2&&column_desc[0].fsrc.u==10&&column_desc[1].fsrc.u==120&&column_desc[0].fsrc.du==100&&column_desc[1].fsrc.du==100,"callback column widths and gap define exact page coordinates");
        for(int col=0;col<2;++col) {
            require(FsQueryTrackParaList(context,column_desc[col].pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].nmp==reinterpret_cast<void*>(101+col),"column continuation covers the next paragraph without restarting");
            require(FsQueryLineListSingle(context,paras[0].pfspara,2,lines,&actual_count)==0&&actual_count==2&&lines[0].urStart==10+110*col&&lines[0].vrStart==20,"queried glyph lines occupy their own columns");
        }
        require(FsQuerySectionBasicColumnList(context,section_desc.pfssection,1,column_desc,&actual_count)==PTS_tserrInvalidParameter&&!actual_count,"short column query rejects without partial output");
        require(FsClearUpdateInfoInPage(context,page_handle)==0&&FsQuerySectionBasicColumnList(context,section_desc.pfssection,2,column_desc,&actual_count)==0&&column_desc[0].fsupdinf.fskupd==fskupdNoChange&&column_desc[1].fsupdinf.fskupd==fskupdNoChange,"update clearing propagates through every column");
        require(FsDestroyPage(context,page_handle)==0&&FsQuerySectionDetails(context,section_desc.pfssection,&section_details)==PTS_tserrInvalidQuery&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"page teardown invalidates section/columns and releases all callback resources");
        available_height=80;hard_break=1;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_break&&formatted.kstop==fmtrBrokenPageBreakBeforePara,"explicit page break before a paragraph stops the page despite spare column space");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryPageSectionList(context,page_handle,1,&section_desc,&actual_count)==0&&FsQuerySectionBasicColumnList(context,section_desc.pfssection,2,column_desc,&actual_count)==0,"page-break section/column query");
        require(FsQueryTrackDetails(context,column_desc[1].pfstrack,&track_details)==0&&track_details.cParas==0,"page break leaves remaining columns empty without consuming following text");
        require(FsDestroyPage(context,page_handle)==0,"explicit page-break page release");incoming_record=page_break;
        require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break&&formatted.kstop==fmtrGoalReached,"pending page break is suppressed exactly once at the destination page start");
        require(FsDestroyPageBreakRecord(context,incoming_record)==0&&FsDestroyPage(context,page_handle)==0&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"explicit page-break continuation cleanup");
        hard_break=2;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break&&formatted.kstop==fmtrGoalReached,"explicit column break resumes within the same page");
        require(FsQueryPageSectionList(context,page_handle,1,&section_desc,&actual_count)==0&&FsQuerySectionBasicColumnList(context,section_desc.pfssection,2,column_desc,&actual_count)==0&&FsQueryTrackParaList(context,column_desc[1].pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].nmp==reinterpret_cast<void*>(102),"column-break paragraph starts at the second column without duplication");
        require(FsDestroyPage(context,page_handle)==0&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"explicit column-break cleanup");hard_break=0;
        std::puts("native explicit breaks: page stop/skipped columns, column resume, destination suppression, exact character ownership and record cleanup PASS");
        invalid_columns=true;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrInvalidGeometry&&!page_handle&&!page_break&&!clients_created,"invalid column extents reject before client allocation");
        invalid_columns=false;fail_line=4;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&!page_break&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"later-column callback failure rolls back earlier-column ownership");
        fail_line=-1;available_height=80;
        std::puts("native column pages: exact source continuation/geometry, owned section/track hierarchy, capacity/update/stale queries, invalid bounds and later-column rollback PASS");
        columns=1;available_height=10;reset_page_counts();
        using BottomlessCreate=int(WINAPI*)(void*,void*,FSFMTRBL*,void**);
        using BottomlessUpdate=int(WINAPI*)(void*,void*,void*,FSFMTRBL*);
        PROC(FsCreatePageBottomless,BottomlessCreate);PROC(FsUpdateBottomlessPage,BottomlessUpdate);
        FSFMTRBL bottomless_result{};
        require(FsCreatePageBottomless(context,reinterpret_cast<void*>(66),&bottomless_result,&page_handle)==0&&bottomless_result==fmtrblGoalReached,"bottomless formatting consumes full text beyond finite available height");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&details.u.simple.trackdescr.fsrc.dv==40,"bottomless page height is actual consumed height");
        void* previous_track=details.u.simple.trackdescr.pfstrack;
        require(FsUpdateBottomlessPage(context,page_handle,reinterpret_cast<void*>(66),&bottomless_result)==0&&lines_created==8&&lines_destroyed==4&&clients_created==4&&clients_destroyed==2,"bottomless update retains page handle and releases superseded callback handles");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&details.u.simple.trackdescr.pfstrack!=previous_track&&FsQueryTrackDetails(context,previous_track,&track_details)==PTS_tserrInvalidQuery,"successful replacement invalidates old layout queries");
        previous_track=details.u.simple.trackdescr.pfstrack;fail_line=lines_created+2;
        require(FsUpdateBottomlessPage(context,page_handle,reinterpret_cast<void*>(66),&bottomless_result)==PTS_tserrCallbackException&&FsQueryPageDetails(context,page_handle,&details)==0&&details.u.simple.trackdescr.pfstrack==previous_track,"failed bottomless replacement preserves the previous valid page and layout");
        fail_line=-1;require(FsDestroyPage(context,page_handle)==0&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"bottomless update/failure/teardown releases every source callback resource");
        available_height=80;
        std::puts("native bottomless pages: full content height, stable page identity, atomic replacement/failed-update retention, stale layout rejection and exact cleanup PASS");
        using SubtrackBottomless=int(WINAPI*)(void*,void*,int,void*,int,uint32_t,int,int,int,void*,FSKCLEAR,int,FSFMTRBL*,void**,int*,FSBBOX*,void**,FSKCLEAR*,int*,int*);
        using SubtrackUpdate=int(WINAPI*)(void*,void*,void*,int,void*,int,uint32_t,int,int,int,void*,FSKCLEAR,int,FSFMTRBL*,int*,FSBBOX*,void**,FSKCLEAR*,int*,int*);
        PROC(FsFormatSubtrackBottomless,SubtrackBottomless);PROC(FsUpdateBottomlessSubtrack,SubtrackUpdate);
        using SubtrackSync=int(WINAPI*)(void*,void*,void*,uint32_t,int);PROC(FsSynchronizeBottomlessSubtrack,SubtrackSync);
        using QuerySubtrack=int(WINAPI*)(void*,void*,FSSUBTRACKDETAILS*);PROC(FsQuerySubtrackDetails,QuerySubtrack);FSSUBTRACKDETAILS updated_track_details{};
        int track_used=0,track_top=0,track_uninterruptible=0;FSBBOX track_box{};void* track_margin=nullptr;FSKCLEAR track_clear{};
        reset_page_counts();
        require(FsFormatSubtrackBottomless(context,reinterpret_cast<void*>(77),0,nullptr,1,0,10,100,20,nullptr,fskclearNone,1,&bottomless_result,&track,&track_used,&track_box,&track_margin,&track_clear,&track_top,&track_uninterruptible)==0&&track_used==40,"standalone bottomless subtrack initial actual extent");
        require(FsQuerySubtrackParaList(context,track,2,paras,&actual_count)==0&&actual_count==2,"standalone subtrack owns original paragraph records");void* previous_text=paras[0].pfspara;para_count=3;
        auto update_track=[&](void* target,int width=100){return FsUpdateBottomlessSubtrack(context,target,reinterpret_cast<void*>(77),0,nullptr,1,0,10,width,40,nullptr,fskclearNone,1,&bottomless_result,&track_used,&track_box,&track_margin,&track_clear,&track_top,&track_uninterruptible);};
        require(update_track(track)==0&&track_used==60&&track_uninterruptible&&FsQuerySubtrackDetails(context,track,&updated_track_details)==0&&updated_track_details.fsrc.v==40&&updated_track_details.fsrc.dv==60&&updated_track_details.cParas==3&&FsQueryTextDetails(context,previous_text,&text_details)==PTS_tserrInvalidQuery,"subtrack retains identity, publishes new source/geometry and invalidates superseded text records");
        require(lines_created==10&&lines_destroyed==4&&clients_created==5&&clients_destroyed==2,"subtrack replacement releases superseded callbacks exactly once");
        FSPARADESCRIPTION updated_paras[3]{};require(FsQuerySubtrackParaList(context,track,3,updated_paras,&actual_count)==0&&actual_count==3&&FsQueryLineListSingle(context,updated_paras[0].pfspara,2,lines,&actual_count)==0,"updated line snapshot before synchronization");void* stable_line=lines[0].pfslineclient;
        require(FsSynchronizeBottomlessSubtrack(context,track,nullptr,0,13)==0&&FsQuerySubtrackDetails(context,track,&updated_track_details)==0&&updated_track_details.fsrc.v==53&&FsQueryLineListSingle(context,updated_paras[0].pfspara,2,lines,&actual_count)==0&&lines[0].vrStart==53&&lines[1].vrStart==63&&lines[0].pfslineclient==stable_line&&lines_created==10,"subtrack shift preserves line identity/source while moving real query geometry without formatting callbacks");
        require(FsSynchronizeBottomlessSubtrack(context,track,nullptr,0,INT_MAX)==PTS_tserrInvalidGeometry&&FsQuerySubtrackDetails(context,track,&updated_track_details)==0&&updated_track_details.fsrc.v==53&&FsQueryLineListSingle(context,updated_paras[0].pfspara,2,lines,&actual_count)==0&&lines[0].vrStart==53,"overflowing shift rejects before changing any retained geometry");
        require(FsSynchronizeBottomlessSubtrack(context,track,reinterpret_cast<void*>(1),0,1)==PTS_tserrNotImplemented&&FsSynchronizeBottomlessSubtrack(context,track,nullptr,4,1)==PTS_tserrNotImplemented&&FsSynchronizeBottomlessSubtrack(context,track,nullptr,0,-13)==0,"unsupported synchronization inputs reject and reverse shift restores original coordinates");
        fail_line=lines_created+3;track_used=123;track_box={1,{1,2,3,4}};track_margin=reinterpret_cast<void*>(1);track_uninterruptible=1;
        require(update_track(track)==PTS_tserrCallbackException&&!track_used&&!track_box.fDefined&&!track_margin&&!track_uninterruptible&&FsQuerySubtrackDetails(context,track,&updated_track_details)==0&&updated_track_details.cParas==3&&updated_track_details.fsrc.dv==60,"failed subtrack replacement preserves prior layout and clears outputs");fail_line=-1;
        int before_bad=clients_created;require(update_track(track,0)==PTS_tserrNotImplemented&&clients_created==before_bad,"invalid subtrack width rejects before callbacks");
        require(update_track(reinterpret_cast<void*>(1))==PTS_tserrInvalidQuery&&!track_used&&!track_box.fDefined,"stale subtrack update rejects with reset outputs");
        require(FsDestroySubtrack(context,track)==0&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed&&update_track(track)==PTS_tserrInvalidQuery,"updated subtrack final teardown and stale identity rejection");para_count=2;
        std::puts("native bottomless subtrack update: stable identity, changed source/extent/position, old text invalidation, partial failure retention, clean outputs and exact callback teardown PASS");
        using QueryFloater=int(WINAPI*)(void*,void*,FSFLOATERDETAILS*);PROC(FsQueryFloaterDetails,QueryFloater);
        block_ui=true;available_height=30;reset_page_counts();float_created=float_destroyed=0;
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_break,"block defers intact after preceding text");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].idobj==-1&&float_created==0,"deferred block retains no content and consumes no source");
        require(FsDestroyPage(context,page_handle)==0,"preceding page disposal");incoming_record=page_break;
        require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break&&FsDestroyPageBreakRecord(context,incoming_record)==0,"block resumes from independent record and finishes document");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].idobj==2&&paras[0].dvrUsed==25,"actual block paragraph geometry");
        void* float_handle=paras[0].pfspara;FSFLOATERDETAILS float_details{};
        require(FsQueryFloaterDetails(context,float_handle,&float_details)==0&&float_details.fsnmFloater==reinterpret_cast<void*>(102)&&float_details.fsrcFloater.u==10&&float_details.fsrcFloater.v==20&&float_details.fsrcFloater.du==100&&float_details.fsrcFloater.dv==25&&float_details.pfsFloaterContent==reinterpret_cast<void*>(102),"query preserves source content handle and page-coordinate block bounds");
        require(FsQueryFloaterDetails(reinterpret_cast<void*>(1),float_handle,&float_details)==PTS_tserrInvalidQuery&&FsClearUpdateInfoInPage(context,page_handle)==0&&FsQueryFloaterDetails(context,float_handle,&float_details)==0&&float_details.fskupdContent==fskupdNoChange,"foreign query rejection and block update state");
        require(FsDestroyPage(context,page_handle)==0&&FsQueryFloaterDetails(context,float_handle,&float_details)==PTS_tserrInvalidQuery&&float_created==float_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"block stale queries and exact complete cleanup");
        available_height=4;reset_page_counts();float_created=float_destroyed=0;incoming_record=nullptr;
        for(int number=0;number<3;++number){
            require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&formatted.fForcedProgress,"oversized line/block accepts actual content on a fresh tiny page");
            if(incoming_record)require(FsDestroyPageBreakRecord(context,incoming_record)==0,"tiny block input disposal");
            require(bool(page_break)==(number<2),"oversized block does not fabricate a terminal continuation");
            require(FsDestroyPage(context,page_handle)==0,"tiny block page disposal");incoming_record=page_break;
        }
        require(float_created==1&&float_destroyed==1&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"forced block complete resource cleanup");
        available_height=80;fail_float=true;reset_page_counts();float_created=float_destroyed=0;
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&!page_break&&float_created==1&&float_destroyed==1&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"failed block callback releases populated content and preceding lines");
        fail_float=false;float_obstacle=true;
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrNotImplemented&&!page_handle&&!page_break&&float_created==float_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"floating obstacle rejects without substituting block semantics or leaking clients");
        float_obstacle=block_ui=false;
        std::puts("native block UI: opaque original handler table, full-width bounds, intact deferral/resume, forced terminal progress, query/update/stale state, callback failure and exact cleanup PASS");
        using SubpageFinite=int(WINAPI*)(void*,void*,int,void*,void*,int,int,uint32_t,int,int,const FSRECT*,int,const FSCOLUMNINFO*,int,int,void* const*,const int*,int,const int*,const int*,int,FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA,FSFMTR*,void**,void**,int*,FSBBOX*,void**,int*);
        using SubpageBottomless=int(WINAPI*)(void*,void*,int,uint32_t,int,int,int,int,int,const FSCOLUMNINFO*,int,void* const*,const int*,int,const int*,const int*,int,FSFMTRBL*,void**,int*,FSBBOX*,void**,int*,int*);
        using QuerySubpage=int(WINAPI*)(void*,void*,FSSUBPAGEDETAILS*);
        using QuerySubColumns=int(WINAPI*)(void*,void*,int,FSTRACKDESCRIPTION*,int*);
        PROC(FsCreateSubpageFinite,SubpageFinite);PROC(FsCreateSubpageBottomless,SubpageBottomless);PROC(FsQuerySubpageDetails,QuerySubpage);PROC(FsQuerySubpageBasicColumnList,QuerySubColumns);
        PROC(FsDestroySubpage,PageAction);PROC(FsDestroySubpageBreakRecord,PageAction);PROC(FsDuplicateSubpageBreakRecord,CloneRecord);PROC(FsClearUpdateInfoInSubpage,PageAction);
        FSSUBPAGEDETAILS sub_details{};FSRECT sub_rect{10,20,100,10};FSCOLUMNINFO sub_columns[2]{{0,100},{10,100}};
        int sub_used=0,sub_top=0;FSBBOX sub_box{};void* sub_margin=nullptr;FSTRACKDESCRIPTION sub_tracks[2]{};
        auto sub_create=[&](void* input,int count=1){return FsCreateSubpageFinite(context,input,1,reinterpret_cast<void*>(77),nullptr,0,1,0,240,140,&sub_rect,count,sub_columns,0,0,nullptr,nullptr,0,nullptr,nullptr,0,fsksuppresshardbreakbeforefirstparaPageAndColumn,&formatted,&page_handle,&page_break,&sub_used,&sub_box,&sub_margin,&sub_top);};
        reset_page_counts();incoming_record=nullptr;
        for(int number=0;number<4;++number) {
            require(sub_create(incoming_record)==0&&page_handle&&bool(page_break)==(number<3),"finite subpage advances its actual source checkpoint");
            if(incoming_record)require(FsDestroySubpageBreakRecord(context,incoming_record)==0,"subpage input release");
            require(FsQueryPageDetails(context,page_handle,&details)==PTS_tserrInvalidQuery&&FsDestroyPage(context,page_handle)==PTS_tserrInvalidQuery,"subpages reject top-level page API ownership");
            require(FsQuerySubpageDetails(context,page_handle,&sub_details)==0&&sub_details.fSimple&&sub_used==10&&FsQueryTrackParaList(context,sub_details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&FsQueryTextDetails(context,paras[0].pfspara,&text_details)==0&&text_details.u.full.dcpFirst==(number%2)*5&&text_details.u.full.dcpLim==(number%2)*5+5,"subpage queries retain actual per-page character ranges");
            require(FsClearUpdateInfoInSubpage(context,page_handle)==0&&FsQuerySubpageDetails(context,page_handle,&sub_details)==0&&sub_details.u.simple.trackdescr.fsupdinf.fskupd==fskupdNoChange,"subpage recursive update clearing");
            if(number==0){void* clone=nullptr;require(FsDuplicateSubpageBreakRecord(context,page_break,&clone)==0&&clone!=page_break&&destroy_subtrack_record(context,clone)==PTS_tserrInvalidBreakRecord&&FsDestroyPageBreakRecord(context,clone)==PTS_tserrInvalidBreakRecord&&FsDestroySubpageBreakRecord(context,page_break)==0,"subpage clones have independent identity and reject other record kinds");page_break=clone;}
            require(FsDestroySubpage(context,page_handle)==0&&FsQuerySubpageDetails(context,page_handle,&sub_details)==PTS_tserrInvalidQuery,"subpage disposal invalidates layout query");incoming_record=page_break;
        }
        require(lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"subpage continuation and clones release every callback resource once");
        structural_line=true;sub_rect.dv=4;reset_page_counts();incoming_record=nullptr;bool structural_coverage[2][10]{};
        for(int number=0;number<2;++number){require(sub_create(incoming_record)==0&&formatted.fForcedProgress&&sub_used==4&&bool(page_break)==(number==0),"structural marker prefix preserves forced visible-line progress");if(incoming_record)require(FsDestroySubpageBreakRecord(context,incoming_record)==0,"structural prefix input release");require(FsQuerySubpageDetails(context,page_handle,&sub_details)==0&&FsQueryTrackParaList(context,sub_details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==(number==0?2:1),"zero-height structural source remains a retained paragraph on the page");for(int index=0;index<actual_count;++index){require(FsQueryTextDetails(context,paras[index].pfspara,&text_details)==0,"structural text query");int owner=int(reinterpret_cast<uintptr_t>(paras[index].nmp))-101;for(int at=text_details.u.full.dcpFirst;at<text_details.u.full.dcpLim;++at){require(owner>=0&&owner<2&&at>=0&&at<10&&!structural_coverage[owner][at],"structural/visible source is consumed once");structural_coverage[owner][at]=true;}}require(FsDestroySubpage(context,page_handle)==0,"structural prefix subpage release");incoming_record=page_break;}
        require(lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"structural and visible lines release all callback resources");structural_line=false;for(const auto& paragraph:structural_coverage)for(bool seen:paragraph)require(seen,"structural and visible source coverage has no gaps");
        sub_rect.du=210;sub_rect.dv=20;reset_page_counts();
        require(sub_create(nullptr,2)==0&&!page_break&&sub_used==20&&FsQuerySubpageDetails(context,page_handle,&sub_details)==0&&!sub_details.fSimple&&sub_details.u.complex.cBasicColumns==2,"finite subpage owns a real two-column hierarchy");
        require(FsQuerySubpageBasicColumnList(context,page_handle,1,sub_tracks,&actual_count)==PTS_tserrInvalidParameter&&!actual_count&&FsQuerySubpageBasicColumnList(context,page_handle,2,sub_tracks,&actual_count)==0&&actual_count==2&&sub_tracks[0].fsrc.u==10&&sub_tracks[1].fsrc.u==120,"subpage columns preserve supplied widths/gaps and reject short arrays");
        require(FsDestroySubpage(context,page_handle)==0&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"column subpage exact teardown");
        sub_columns[1].durWidth=250;int previous_clients=clients_created;
        require(sub_create(nullptr,2)==PTS_tserrInvalidGeometry&&!page_handle&&!page_break&&clients_created==previous_clients,"invalid subpage columns reject before client allocation");sub_columns[1].durWidth=100;
        sub_rect.du=100;sub_rect.dv=10;fail_line=lines_created+2;
        require(sub_create(nullptr)==PTS_tserrCallbackException&&!page_handle&&!page_break&&!sub_used&&!sub_box.fDefined&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"failed subpage clears metric outputs and releases populated line callbacks");fail_line=-1;
        int uninterruptible=0;reset_page_counts();
        using SubpageUpdate=int(WINAPI*)(void*,void*,void*,int,uint32_t,int,int,int,int,int,const FSCOLUMNINFO*,int,void* const*,const int*,int,const int*,const int*,int,FSFMTRBL*,int*,FSBBOX*,void**,int*,int*);
        PROC(FsUpdateBottomlessSubpage,SubpageUpdate);
        require(FsCreateSubpageBottomless(context,reinterpret_cast<void*>(77),1,0,240,10,100,20,1,sub_columns,0,nullptr,nullptr,0,nullptr,nullptr,1,&bottomless_result,&page_handle,&sub_used,&sub_box,&sub_margin,&sub_top,&uninterruptible)==0&&sub_used==40&&uninterruptible&&FsQuerySubpageDetails(context,page_handle,&sub_details)==0&&sub_details.u.simple.trackdescr.fsrc.dv==40,"bottomless subpage reports full actual extent and completed uninterruptible formatting");
        void* stable_subpage=page_handle;void* old_subtrack=sub_details.u.simple.trackdescr.pfstrack;para_count=3;
        auto sub_update=[&](void* target){return FsUpdateBottomlessSubpage(context,target,reinterpret_cast<void*>(77),1,0,240,10,100,30,1,sub_columns,0,nullptr,nullptr,0,nullptr,nullptr,1,&bottomless_result,&sub_used,&sub_box,&sub_margin,&sub_top,&uninterruptible);};
        require(sub_update(page_handle)==0&&page_handle==stable_subpage&&sub_used==60&&uninterruptible&&FsQuerySubpageDetails(context,page_handle,&sub_details)==0&&sub_details.u.simple.trackdescr.fsrc.v==30&&sub_details.u.simple.trackdescr.fsrc.dv==60&&sub_details.u.simple.trackdescr.pfstrack!=old_subtrack&&FsQueryTrackDetails(context,old_subtrack,&track_details)==PTS_tserrInvalidQuery,"bottomless subpage replacement keeps identity and publishes actual new geometry while invalidating the old track");
        require(lines_created==10&&lines_destroyed==4&&clients_created==5&&clients_destroyed==2,"successful update releases superseded original clients and lines once");
        old_subtrack=sub_details.u.simple.trackdescr.pfstrack;fail_line=lines_created+3;sub_used=123;sub_box={1,{1,2,3,4}};sub_margin=reinterpret_cast<void*>(1);uninterruptible=1;
        require(sub_update(page_handle)==PTS_tserrCallbackException&&!sub_used&&!sub_box.fDefined&&!sub_margin&&!uninterruptible&&FsQuerySubpageDetails(context,page_handle,&sub_details)==0&&sub_details.u.simple.trackdescr.pfstrack==old_subtrack,"failed subpage update resets outputs and preserves the last valid tree");fail_line=-1;
        require(sub_update(reinterpret_cast<void*>(1))==PTS_tserrInvalidQuery&&!sub_used&&!sub_box.fDefined&&!sub_margin&&!uninterruptible,"stale subpage update rejects with clean outputs");para_count=2;
        int before_invalid=clients_created;sub_columns[0].durWidth=250;
        require(sub_update(page_handle)==PTS_tserrInvalidGeometry&&clients_created==before_invalid&&!sub_used&&!sub_box.fDefined&&FsQuerySubpageDetails(context,page_handle,&sub_details)==0&&sub_details.u.simple.trackdescr.pfstrack==old_subtrack,"invalid replacement geometry rejects before callbacks and preserves old layout");sub_columns[0].durWidth=100;
        require(FsUpdateBottomlessSubpage(reinterpret_cast<void*>(1),page_handle,reinterpret_cast<void*>(77),1,0,240,10,100,30,1,sub_columns,0,nullptr,nullptr,0,nullptr,nullptr,1,&bottomless_result,&sub_used,&sub_box,&sub_margin,&sub_top,&uninterruptible)==PTS_tserrInvalidQuery,"foreign context cannot replace a live subpage");
        require(FsUpdateBottomlessSubpage(context,page_handle,reinterpret_cast<void*>(78),1,0,240,10,100,30,1,sub_columns,0,nullptr,nullptr,0,nullptr,nullptr,1,&bottomless_result,&sub_used,&sub_box,&sub_margin,&sub_top,&uninterruptible)==PTS_tserrInvalidQuery,"different segment cannot hijack a live subpage");
        require(FsDestroySubpage(context,page_handle)==0&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"bottomless subpage exact teardown");
        require(sub_update(stable_subpage)==PTS_tserrInvalidQuery,"destroyed subpage cannot be updated");
        std::puts("native bottomless subpage update: stable identity, new source/extent, invalidated old track, failed-update retention, clean outputs and exact callback cleanup PASS");
        std::puts("native subpages: finite ranges, independent cloned records/kinds, column hierarchy/geometry, recursive update/stale queries, failure rollback, bottomless extent and exact cleanup PASS");
        subpage_parent=true;available_height=10;reset_page_counts();incoming_record=nullptr;
        for(int number=0;number<4;++number){require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&bool(page_break)==(number<3),"installed subpage objects propagate their typed continuations through root pages");if(incoming_record)require(FsDestroyPageBreakRecord(context,incoming_record)==0,"parent input release");require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].idobj==1&&FsQuerySubpageDetails(context,paras[0].pfspara,&sub_details)==0,"root paragraph owns actual installed subpage result");require(FsDestroyPage(context,page_handle)==0,"parent subpage page cleanup");incoming_record=page_break;}
        require(lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"typed child continuations and nested client ownership release once");
        available_height=4;reset_page_counts();incoming_record=nullptr;
        for(int number=0;number<4;++number){require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&formatted.fForcedProgress&&bool(page_break)==(number<3),"subpage forced ink overhang makes bounded parent progress");if(incoming_record)require(FsDestroyPageBreakRecord(context,incoming_record)==0,"forced subpage parent input release");require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].dvrUsed==4&&paras[0].fsbbox.fsrc.dv==10,"parent accounts for available height while preserving actual oversized subpage ink");require(FsDestroyPage(context,page_handle)==0,"forced parent page release");incoming_record=page_break;}
        require(lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"forced nested subpages release resources once");
        lose_subtrack=true;reset_page_counts();
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&!page_break&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"lost successful subpage and break outputs roll back complete callback transaction");lose_subtrack=false;subpage_parent=false;
        std::puts("native installed subpage objects: typed nested continuation, independent root/page lifetime, client ownership and lost-output callback rollback PASS");
        using QueryAttached=int(WINAPI*)(void*,void*,int,FSATTACHEDOBJECTDESCRIPTION*,int*);PROC(FsQueryAttachedObjectList,QueryAttached);
        using QueryFigure=int(WINAPI*)(void*,void*,FSFIGUREDETAILS*);PROC(FsQueryFigureObjectDetails,QueryFigure);
        available_height=80;para_count=1;attached_figure=true;
        for(int alignment:{0,1,2}) {
            figure_alignment=alignment;reset_page_counts();
            require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break,"figure attachments format through original content and position callbacks");
            require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0,"figure host track query");
            FSATTACHEDOBJECTDESCRIPTION object{};FSFIGUREDETAILS figure_details{};
            require(FsQueryAttachedObjectList(context,paras[0].pfspara,1,&object,&actual_count)==0&&actual_count==1&&object.idobj==-2&&object.nmp==reinterpret_cast<void*>(301),"figure query exposes original source name and managed client");
            require(FsQueryFigureObjectDetails(context,object.pfspara,&figure_details)==0&&figure_details.fsrcFlowAround.u==10+30*alignment&&figure_details.fsrcFlowAround.v==20&&figure_details.fsrcFlowAround.du==40&&figure_details.fsrcFlowAround.dv==25,"actual left/center/right figure details agree with callback geometry");
            require(FsQueryFigureObjectDetails(reinterpret_cast<void*>(999),object.pfspara,&figure_details)==PTS_tserrInvalidQuery&&!figure_details.fsbbox.fDefined&&FsQueryFigureObjectDetails(context,object.pfspara,nullptr)==PTS_tserrNullOutputParameter,"figure query validates context and resets rejected output");
            require(FsClearUpdateInfoInPage(context,page_handle)==0&&FsDestroyPage(context,page_handle)==0&&FsQueryFigureObjectDetails(context,object.pfspara,&figure_details)==PTS_tserrInvalidQuery&&figure_content.empty()&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"figure content/client/line resources release exactly once and stale handles reject");
        }
        for(int failure:{0,1}) {
            figure_fail_properties=failure==0;figure_fail_position=failure==1;reset_page_counts();
            require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&!page_break&&figure_content.empty()&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"lost subpage and populated position failure clean up all speculative original resources");
        }
        figure_fail_properties=figure_fail_position=attached_figure=false;
        std::puts("native figures: original subpage/position callbacks, source geometry, typed/capacity/context/stale queries, lost-output and position failure cleanup PASS");
        available_height=80;para_count=1;attached_float=true;
        for(bool right:{false,true}) {
            attached_right=right;reset_page_counts();float_created=float_destroyed=0;
            require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break,"attached rectangular floater formats through original handler callbacks");
            require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&FsQueryTextDetails(context,paras[0].pfspara,&text_details)==0&&text_details.u.full.cAttachedObjects==1,"actual attached-object count in text details");
            FSATTACHEDOBJECTDESCRIPTION object{};require(FsQueryAttachedObjectList(context,paras[0].pfspara,0,&object,&actual_count)==PTS_tserrInvalidParameter&&!actual_count&&FsQueryAttachedObjectList(context,paras[0].pfspara,1,&object,&actual_count)==0&&actual_count==1&&object.idobj==2&&object.nmp==reinterpret_cast<void*>(301)&&object.pfsparaclient==reinterpret_cast<void*>(301),"attachment query has real original client/name and capacity semantics");
            require(FsQueryFloaterDetails(context,object.pfspara,&float_details)==0&&float_details.fsrcFloater.u==(right?70:10)&&float_details.fsrcFloater.du==40&&float_details.fsrcFloater.dv==25,"floating rectangle uses requested horizontal alignment");
            require(FsQueryLineListSingle(context,paras[0].pfspara,2,lines,&actual_count)==0&&actual_count==2&&lines[0].urStart==(right?10:50)&&lines[0].dur==60,"text line is formatted within the actual space beside the floater");
            require(FsClearUpdateInfoInPage(context,page_handle)==0&&FsQueryAttachedObjectList(context,paras[0].pfspara,1,&object,&actual_count)==0&&object.fsupdinf.fskupd==fskupdNoChange,"attachment update state clears with its page");
            require(FsDestroyPage(context,page_handle)==0&&FsQueryAttachedObjectList(context,paras[0].pfspara,1,&object,&actual_count)==PTS_tserrInvalidQuery&&float_created==1&&float_destroyed==1&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"attached content/client/line lifetime and stale text-query rejection");
        }
        multiple_float=true;attached_right=false;
        for(bool opposite:{false,true}) {
            multiple_opposite=opposite;reset_page_counts();float_created=float_destroyed=0;
            require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break,"multiple original attachments format in source order");
            require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].dvrUsed==(opposite?25:50),"multiple floater paragraph extent includes actual collision-free placements");
            FSATTACHEDOBJECTDESCRIPTION objects[2]{};require(FsQueryAttachedObjectList(context,paras[0].pfspara,1,objects,&actual_count)==PTS_tserrInvalidParameter&&!actual_count&&FsQueryAttachedObjectList(context,paras[0].pfspara,2,objects,&actual_count)==0&&actual_count==2,"multiple attachment query capacity and actual count");
            FSFLOATERDETAILS first{},second{};require(FsQueryFloaterDetails(context,objects[0].pfspara,&first)==0&&FsQueryFloaterDetails(context,objects[1].pfspara,&second)==0&&first.fsrcFloater.u==10&&first.fsrcFloater.v==20&&second.fsrcFloater.u==(opposite?70:10)&&second.fsrcFloater.v==(opposite?20:45),"same-edge rectangles stack and opposite edges share nonoverlapping row");
            require(FsDestroyPage(context,page_handle)==0&&float_created==2&&float_destroyed==2&&lines_created==lines_destroyed&&breaks_destroyed==lines_created&&clients_created==clients_destroyed,"multiple attached content and clients release once");
        }
        multiple_opposite=false;float_fail_at=2;reset_page_counts();float_created=float_destroyed=0;
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&!page_break&&float_created==2&&float_destroyed==2&&clients_created==clients_destroyed,"later populated floater failure releases prior accepted and current speculative attachments");float_fail_at=-1;
        available_height=40;reset_page_counts();float_created=float_destroyed=0;
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrNotImplemented&&!page_handle&&!page_break&&float_created==2&&float_destroyed==2&&clients_created==clients_destroyed,"unsupported multi-floater overflow retains no partial page or attachments");available_height=80;multiple_float=false;
        for(int deferred_mode:{0,1,2}) {
            deferred_attachment=true;multiple_float=deferred_mode==1;split_attachment=deferred_mode==2;para_count=2;available_height=multiple_float?60:40;reset_page_counts();float_created=float_destroyed=float_records_destroyed=0;
            require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_break&&formatted.kstop==fmtrBrokenOutOfSpace,"occupied page defers a floater without consuming its host anchor");
            require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].nmp==reinterpret_cast<void*>(101)&&float_created==float_destroyed&&float_records_destroyed==(split_attachment?1:0),"deferred paragraph has no page-owned speculative attachment");
            require(FsDestroyPage(context,page_handle)==0,"destroy page before resuming deferred attachment");
            void* deferred_record=page_break;
            require(FsCreatePageFinite(context,deferred_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break&&float_created-float_destroyed==(multiple_float?2:1),"deferred floater resumes intact on fresh page");
            require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].nmp==reinterpret_cast<void*>(102)&&FsQueryLineListSingle(context,paras[0].pfspara,2,lines,&actual_count)==0&&actual_count==2&&lines[0].dcpFirst==0,"resumed host starts at original unconsumed anchor");
            require(FsDestroyPage(context,page_handle)==0&&FsDestroyPageBreakRecord(context,deferred_record)==0&&float_created==float_destroyed&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"deferred attachment and incoming record release exactly once");
        }
        deferred_attachment=split_attachment=multiple_float=false;available_height=80;para_count=1;
        std::puts("native deferred floaters: nonzero no-progress dimensions, speculative subpage/break-record cleanup, collision rollback, exact host-anchor resume and object lifetime PASS");
        for(int mode:{1,2,6}) {
            inline_anchor_mode=mode;reset_page_counts();float_created=float_destroyed=0;
            require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break,"in-line anchors are discovered and wrapped through original callbacks");
            require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1,"in-line paragraph retains its real native text record");
            FSATTACHEDOBJECTDESCRIPTION objects[2]{};
            require(FsQueryAttachedObjectList(context,paras[0].pfspara,2,objects,&actual_count)==0&&actual_count==(mode==6?2:1)&&FsQueryFloaterDetails(context,objects[actual_count-1].pfspara,&float_details)==0&&float_details.fsrcFloater.v==(mode==6?45:mode==2?30:20),"inline objects follow moved anchors and actual line positions after nonwrapping obstacles");
            FSLINEDESCRIPTIONSINGLE inline_lines[3]{};
            require(FsQueryLineListSingle(context,paras[0].pfspara,3,inline_lines,&actual_count)==0&&actual_count==(mode==2?3:2)&&inline_lines[0].dcpFirst==0&&inline_lines[actual_count-1].dcpLim==10,"in-line reformat retains exact original text extent");
            for(int i=1;i<actual_count;++i)require(inline_lines[i-1].dcpLim==inline_lines[i].dcpFirst,"inline line source intervals are contiguous");
            require(FsDestroyPage(context,page_handle)==0&&float_created==(mode==6?2:1)&&float_destroyed==float_created&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"inline retries retain one floating object and release every speculative line");
        }
        for(int mode:{3,4,5}) {
            inline_anchor_mode=mode;reset_page_counts();float_created=float_destroyed=0;
            require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==(mode==5?PTS_tserrCallbackException:PTS_tserrInvalidClientOutput)&&!page_handle&&!page_break&&float_created==float_destroyed&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"invalid inline counts/arrays or failed discovery release owned line and client outputs");
        }
        inline_anchor_mode=0;
        std::puts("native inline floaters: discovery, constrained line retry, moved anchor placement, exact source/ownership, invalid outputs and callback cleanup PASS");
        paginate_float=true;float_records_created=float_records_destroyed=0;reset_page_counts();float_created=float_destroyed=0;
        FSRECT floating_area{10,20,100,80};
        require(format_subtrack(context,nullptr,0,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&floating_area,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==0&&subrecord&&float_live_records.size()==1,"split floater produces an owned content continuation");
        require(FsDuplicateSubtrackBreakRecord(context,subrecord,&clone)==0&&clone!=subrecord&&destroy_subtrack_record(context,subrecord)==0&&destroy_subtrack(context,subtrack)==0&&float_live_records.size()==1,"cloned continuation retains original floater record after page/subtrack destruction");
        FSRECT unavailable_floating_area{10,20,100,10};
        require(format_subtrack(context,clone,1,reinterpret_cast<void*>(77),0,nullptr,nullptr,1,1,0,&unavailable_floating_area,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==0&&subrecord&&used==0&&formatted.kstop==fmtrNoProgressOutOfSpace&&float_live_records.size()==1,"no-progress resumed floater preserves the owned incoming content checkpoint");
        require(destroy_subtrack_record(context,subrecord)==0&&destroy_subtrack(context,subtrack)==0&&float_live_records.size()==1,"discarded no-progress clone releases wrappers without consuming its input");
        fail_float=true;
        require(format_subtrack(context,clone,1,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&floating_area,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==PTS_tserrCallbackException&&!subtrack&&!subrecord&&float_live_records.size()==1,"failed split resume releases speculative content and output record while preserving input");fail_float=false;
        void* branches[2]{};
        for(int branch=0;branch<2;++branch){
            require(format_subtrack(context,clone,1,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&floating_area,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&branches[branch],&used,&bbox,&margin,&clear,&top_space)==0&&branches[branch]&&used==25,"immutable floating continuation can resume independently in two branches");
            require(FsQuerySubtrackParaList(context,subtrack,1,paras,&actual_count)==0&&actual_count==1&&FsQueryTextDetails(context,paras[0].pfspara,&text_details)==0&&text_details.u.full.cLines==0&&text_details.u.full.cAttachedObjects==1,"floating-only page chunk does not consume host text again");
            require(destroy_subtrack(context,subtrack)==0,"release independent intermediate floating content");
        }
        require(destroy_subtrack_record(context,clone)==0&&float_live_records.size()==2,"input floating record releases after its last independent continuation owner");
        for(void* branch:branches){
            require(format_subtrack(context,branch,1,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&floating_area,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==0&&!subrecord&&used==25,"final floating content chunk completes without a phantom continuation");
            require(destroy_subtrack_record(context,branch)==0&&destroy_subtrack(context,subtrack)==0,"terminal floating content and input record release independently");
        }
        require(float_live_records.empty()&&float_records_created==float_records_destroyed&&float_created==float_destroyed&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"split/cloned/failed/terminal floating paths release every content/client/record/line once");paginate_float=false;
        std::puts("native split floaters: owned subpage records, host-terminal chunks, independent cloned resumes, populated callback failure and exact final cleanup PASS");
        fail_float=true;reset_page_counts();float_created=float_destroyed=0;
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&!page_break&&float_created==float_destroyed&&lines_created==lines_destroyed&&clients_created==clients_destroyed,"populated attached-content callback failure tears down all owned objects");
        fail_float=attached_float=attached_right=false;para_count=2;
        std::puts("native attached floaters: original callbacks, multiple collision-free placements/later failure, left/right rectangular wrapping, attached count/query/update/capacity, stale handles, partial failure and exact cleanup PASS");
        using QueryTable=int(WINAPI*)(void*,void*,FSTABLEOBJDETAILS*);using QueryTableProper=int(WINAPI*)(void*,void*,FSTABLEDETAILS*);using QueryTableRows=int(WINAPI*)(void*,void*,int,FSTABLEROWDESCRIPTION*,int*);using QueryTableRow=int(WINAPI*)(void*,void*,FSTABLEROWDETAILS*);using QueryTableCells=int(WINAPI*)(void*,void*,int,FSKUPDATE*,void**,FSTABLEKCELLMERGE*,int*);
        PROC(GetTableObjHandlerInfo,int(WINAPI*)(const FSTABLEOBJINIT*,void*));table_handler=GetTableObjHandlerInfo;clear_cell_subpage=FsClearUpdateInfoInSubpage;
        PROC(FsQueryTableObjDetails,QueryTable);PROC(FsQueryTableObjTableProperDetails,QueryTableProper);PROC(FsQueryTableObjRowList,QueryTableRows);PROC(FsQueryTableObjRowDetails,QueryTableRow);PROC(FsQueryTableObjCellList,QueryTableCells);
        table_mode=true;para_count=1;available_height=48;reset_page_counts();table_cells_created=table_cells_destroyed=0;
        FSTABLEOBJDETAILS table_details{};FSTABLEDETAILS table_proper{};FSTABLEROWDESCRIPTION table_rows[3]{};FSTABLEROWDETAILS table_row{};void* cell_handles[2]{};FSKUPDATE cell_updates[2]{};FSTABLEKCELLMERGE cell_merges[2]{};
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_break,"table formats real owned cell subpages and defers an intact later row");
        require(FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,1,paras,&actual_count)==0&&actual_count==1&&paras[0].idobj==3&&paras[0].dvrUsed==44,"table paragraph uses actual maximum cell heights and row spacing");
        void* table_handle=paras[0].pfspara;
        require(FsQueryTableObjDetails(context,table_handle,&table_details)==0&&table_details.fsrcTableObj.v==20&&table_details.fsrcTableObj.du==100&&FsQueryTableObjTableProperDetails(context,table_details.pfstableProper,&table_proper)==0&&table_proper.cRows==2&&table_proper.dvrTable==44,"actual table geometry and proper-table query records");
        require(FsQueryTableObjRowList(context,table_handle,1,table_rows,&actual_count)==PTS_tserrInvalidParameter&&!actual_count&&FsQueryTableObjRowList(context,table_handle,3,table_rows,&actual_count)==0&&actual_count==2&&table_rows[0].fsnmRow==reinterpret_cast<void*>(501)&&table_rows[1].fsnmRow==reinterpret_cast<void*>(502),"table row query capacity and original row identities");
        require(FsQueryTableObjRowDetails(context,table_rows[1].pfstablerow,&table_row)==0&&table_row.cCells==2&&table_row.fskboundaryBelow==fsktablerowboundaryBreak&&FsQueryTableObjCellList(context,table_rows[0].pfstablerow,2,cell_updates,cell_handles,cell_merges,&actual_count)==0&&actual_count==2&&cell_handles[0]&&cell_handles[1]&&cell_merges[0]==fskcellmergeNo,"row continuation boundary and original opaque cell handles");
        require(FsClearUpdateInfoInPage(context,page_handle)==0&&FsQueryTableObjDetails(context,table_handle,&table_details)==0&&table_details.fskupdTableProper==fskupdNoChange&&FsQueryTableObjCellList(context,table_rows[0].pfstablerow,2,cell_updates,cell_handles,cell_merges,&actual_count)==0&&cell_updates[0]==fskupdNoChange,"table and cell update clearing follows owned original callbacks");
        incoming_record=page_break;
        require(FsDestroyPage(context,page_handle)==0&&FsQueryTableObjDetails(context,table_handle,&table_details)==PTS_tserrInvalidQuery&&FsQueryTableObjRowDetails(context,table_rows[0].pfstablerow,&table_row)==PTS_tserrInvalidQuery,"destroyed table and row handles reject stale queries");
        require(FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break&&FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,1,paras,&actual_count)==0&&FsQueryTableObjRowList(context,paras[0].pfspara,3,table_rows,&actual_count)==0&&actual_count==1&&table_rows[0].fsnmRow==reinterpret_cast<void*>(503),"table continuation resumes the unconsumed row after old page destruction");
        require(FsDestroyPageBreakRecord(context,incoming_record)==0&&FsDestroyPage(context,page_handle)==0&&table_cells_created==6&&table_cells_destroyed==6&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"table/cell/subpage/text resources release exactly once across pages");
        for(bool lost:{false,true}){reset_page_counts();table_cells_created=table_cells_destroyed=0;table_fail_cell=lost?-1:2;lose_table_cell=lost;
            require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==PTS_tserrCallbackException&&!page_handle&&!page_break&&table_cells_created==table_cells_destroyed&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"failed populated or lost cell outputs release every prior and speculative native subpage");
        }
        table_fail_cell=-1;lose_table_cell=false;split_table_cells=true;table_rows_count=1;reset_page_counts();table_cells_created=table_cells_destroyed=table_records_created=table_records_destroyed=0;
        FSRECT split_row_area{10,20,100,10};
        require(format_subtrack(context,nullptr,0,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&split_row_area,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==0&&subrecord&&used==10&&table_records_created==1,"split row preserves an actual cell subpage continuation and a completed neighboring cell");
        require(FsDuplicateSubtrackBreakRecord(context,subrecord,&clone)==0&&clone!=subrecord&&destroy_subtrack_record(context,subrecord)==0&&destroy_subtrack(context,subtrack)==0&&table_records_destroyed==0,"cloned row continuation keeps cell records alive after the original track and wrapper are destroyed");
        FSRECT no_row_space{10,20,100,10};
        require(format_subtrack(context,clone,1,reinterpret_cast<void*>(77),0,nullptr,nullptr,1,1,0,&no_row_space,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==0&&subrecord&&used==0&&formatted.kstop==fmtrNoProgressOutOfSpace&&table_records_destroyed==0,"no-progress split row retains the original cell checkpoint");
        require(destroy_subtrack(context,subtrack)==0&&destroy_subtrack_record(context,subrecord)==0,"discarded row no-progress outputs release without consuming input");
        FSRECT resumed_row_area{10,20,100,80};table_fail_cell=table_cells_created+1;
        require(format_subtrack(context,clone,1,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&resumed_row_area,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==PTS_tserrCallbackException&&!subtrack&&!subrecord&&table_records_destroyed==0,"failed populated resumed cell preserves the input record and releases its temporary page");table_fail_cell=-1;
        for(int branch=0;branch<2;++branch){
            require(format_subtrack(context,clone,1,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&resumed_row_area,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==0&&!subrecord&&used==12,"independent cloned row resumes complete from the same immutable cell input");
            require(FsQuerySubtrackParaList(context,subtrack,1,paras,&actual_count)==0&&FsQueryTableObjRowList(context,paras[0].pfspara,3,table_rows,&actual_count)==0&&actual_count==1&&FsQueryTableObjCellList(context,table_rows[0].pfstablerow,2,cell_updates,cell_handles,cell_merges,&actual_count)==0&&actual_count==2&&!cell_handles[0]&&cell_handles[1],"completed cell has no repeated content handle on later row chunks");
            require(destroy_subtrack(context,subtrack)==0,"independent terminal row content release");
        }
        require(destroy_subtrack_record(context,clone)==0&&table_records_created==table_records_destroyed&&table_cells_created==table_cells_destroyed&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"split/cloned/failed/no-progress/terminal row cases release every cell/record/client/line once");split_table_cells=false;table_rows_count=3;
        std::puts("native split tables: actual cell records, completed-cell omission, immutable cloned resumes, no-progress preservation, populated failure and exact final teardown PASS");
        table_spans=true;table_rows_count=3;available_height=80;reset_page_counts();table_cells_created=table_cells_destroyed=0;span_set_height=0;
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break&&FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,1,paras,&actual_count)==0&&paras[0].dvrUsed==66&&FsQueryTableObjRowList(context,paras[0].pfspara,3,table_rows,&actual_count)==0&&actual_count==3,"connected span group uses natural ordinary row heights and internal spacing");
        void* spanning_handle=nullptr;for(int r=0;r<3;++r){require(FsQueryTableObjCellList(context,table_rows[r].pfstablerow,2,cell_updates,cell_handles,cell_merges,&actual_count)==0&&actual_count==2&&cell_merges[1]==(r==0?fskcellmergeFirst:r==2?fskcellmergeLast:fskcellmergeMiddle),"original first/middle/last merge states are returned");if(r==0)spanning_handle=cell_handles[1];else require(cell_handles[1]==spanning_handle,"spanning rows share one actual cell client");}
        require(span_set_height==64&&table_cells_created==4&&FsDestroyPage(context,page_handle)==0&&table_cells_destroyed==4&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"span height includes all constituent rows and shared cell content releases once");
        span_prefix=true;para_count=2;available_height=80;reset_page_counts();table_cells_created=table_cells_destroyed=0;
        require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&page_break&&table_cells_created==table_cells_destroyed&&FsQueryPageDetails(context,page_handle,&details)==0&&FsQueryTrackParaList(context,details.u.simple.trackdescr.pfstrack,2,paras,&actual_count)==0&&actual_count==1&&paras[0].idobj==-1,"span group that does not fit remaining space defers intact and releases measured cells");incoming_record=page_break;
        require(FsDestroyPage(context,page_handle)==0&&FsCreatePageFinite(context,incoming_record,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==0&&!page_break&&FsDestroyPageBreakRecord(context,incoming_record)==0&&FsDestroyPage(context,page_handle)==0&&table_cells_created==table_cells_destroyed&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"deferred span group resumes after prior page release with exact cell ownership");span_prefix=false;para_count=1;
        for(int failure:{0,1}){reset_page_counts();table_cells_created=table_cells_destroyed=0;table_fail_cell=failure==0?2:-1;malformed_span=failure==1;
            require(FsCreatePageFinite(context,nullptr,reinterpret_cast<void*>(66),&formatted,&page_handle,&page_break)==(failure==0?PTS_tserrCallbackException:PTS_tserrInvalidClientOutput)&&!page_handle&&!page_break&&table_cells_created==table_cells_destroyed&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"span partial callback failure or missing master releases all temporary native resources");
        }
        table_fail_cell=-1;malformed_span=false;long_span=true;reset_page_counts();table_cells_created=table_cells_destroyed=table_records_created=table_records_destroyed=0;
        FSRECT large_span_area{10,20,100,80};
        require(format_subtrack(context,nullptr,0,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&large_span_area,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==0&&subrecord&&used<=80&&table_records_created>0,"oversized connected span group creates actual owned cell continuation records");
        require(FsDuplicateSubtrackBreakRecord(context,subrecord,&clone)==0&&clone!=subrecord&&destroy_subtrack_record(context,subrecord)==0&&destroy_subtrack(context,subtrack)==0&&table_records_destroyed==0,"span record clone retains input after measured and first-chunk content release");
        FSRECT span_no_space{10,20,100,5};
        require(format_subtrack(context,clone,1,reinterpret_cast<void*>(77),0,nullptr,nullptr,1,1,0,&span_no_space,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==0&&subrecord&&used==0&&formatted.kstop==fmtrNoProgressOutOfSpace,"no-progress span resume preserves original cell checkpoints");
        require(destroy_subtrack(context,subtrack)==0&&destroy_subtrack_record(context,subrecord)==0,"unaccepted span fragment outputs release independently");
        table_fail_cell=table_cells_created+1;
        require(format_subtrack(context,clone,1,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&large_span_area,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==PTS_tserrCallbackException&&!subtrack&&!subrecord,"populated span resume failure releases speculative content");table_fail_cell=-1;
        for(int branch=0;branch<2;++branch){require(format_subtrack(context,clone,1,reinterpret_cast<void*>(77),0,nullptr,nullptr,0,1,0,&large_span_area,nullptr,fskclearNone,fsksuppresshardbreakbeforefirstparaNone,&formatted,&subtrack,&subrecord,&used,&bbox,&margin,&clear,&top_space)==0&&!subrecord&&FsQuerySubtrackParaList(context,subtrack,1,paras,&actual_count)==0&&FsQueryTableObjRowList(context,paras[0].pfspara,3,table_rows,&actual_count)==0&&actual_count==3,"cloned span inputs resume independently to completion");
            for(int r=0;r<3;++r)require(FsQueryTableObjCellList(context,table_rows[r].pfstablerow,2,cell_updates,cell_handles,cell_merges,&actual_count)==0&&!cell_handles[0]&&cell_handles[1],"completed ordinary cells are omitted while the resumed span stays shared across rows");
            require(destroy_subtrack(context,subtrack)==0,"terminal span branch release");
        }
        require(destroy_subtrack_record(context,clone)==0&&table_records_created==table_records_destroyed&&table_cells_created==table_cells_destroyed&&clients_created==clients_destroyed&&lines_created==lines_destroyed&&breaks_destroyed==lines_created,"split span clones/no-progress/failure/final release own every cell, record and original line exactly once");long_span=false;
        std::puts("native split spans: actual cell records, shared resumed master/omitted completed cells, immutable clones, no-progress/failure preservation and exact teardown PASS");
        table_fail_cell=-1;malformed_span=table_spans=false;
        std::puts("native vertical spans: original shared client/merge states, combined row height, intact deferral/resume, partial failures and exact single-owner cleanup PASS");
        table_fail_cell=-1;lose_table_cell=table_mode=false;para_count=2;available_height=80;
        std::puts("native tables: original cell/subpage callbacks, exact rows/heights/spacing, typed queries/capacity/update/stale state, row resume and populated/lost-output cleanup PASS");
        columns=1;require(DestroyDocContext(context)==0&&DestroyInstalledObjectsInfo(installed)==0,"failed page creation leaves no retained resources");
        std::puts("native finite pages: actual callback geometry/ranges/break records, query/update state, foreign/stale handles, exact teardown, partial failure rollback and unsupported paths PASS");
        std::printf("native paragraph records: context=%zu, methods=%zu, callbacks=%zu; copied ownership, callbacks, rollback, invalid states and teardown PASS\n",sizeof(FSCONTEXTINFO),sizeof(FSIMETHODS),sizeof(FSCBK));return 0;
    }catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}
}
