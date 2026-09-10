/*******************************************************

   CoolReader Engine

   drawutil.cpp: border and background painting used by DrawDocument()

   This source code is distributed under the terms of
   GNU General Public License
   See LICENSE file for details

*******************************************************/

#include "crsetup.h"

#include "../include/lvtinydom.h"
#include "../include/fb2def.h"
#include "../include/lvrend.h"
#include "../include/drawutil.h"

//draw border lines,support color,width,all styles, not support border-collapse
void DrawBorder(ldomNode *enode,LVDrawBuf & drawbuf,int x0,int y0,int doc_x,int doc_y,RenderRectAccessor fmt)
{
    css_style_ref_t style = enode->getStyle();
    const bool invert_colors = drawbuf.getInvertColors();
    bool hastopBorder = (style->border_style_top >=css_border_solid);
    bool hasrightBorder = (style->border_style_right >=css_border_solid);
    bool hasbottomBorder = (style->border_style_bottom >=css_border_solid);
    bool hasleftBorder = (style->border_style_left >=css_border_solid);

    // Check for explicit 'border-width: 0' which means no border.
    css_length_t bw;
    bw = style->border_width[0];
    hastopBorder = hastopBorder & !(bw.value == 0 && bw.type > css_val_unspecified);
    bw = style->border_width[1];
    hasrightBorder = hasrightBorder & !(bw.value == 0 && bw.type > css_val_unspecified);
    bw = style->border_width[2];
    hasbottomBorder = hasbottomBorder & !(bw.value == 0 && bw.type > css_val_unspecified);
    bw = style->border_width[3];
    hasleftBorder = hasleftBorder & !(bw.value == 0 && bw.type > css_val_unspecified);

    // We have css_val_unspecified only when css_generic_currentcolor, and we should use the current text color.
    // If it is transparent, we have nothing to draw.
    lUInt32 topBordercolor = style->border_color[0].type != css_val_unspecified ? style->border_color[0].value : style->color.value;
    hastopBorder = hastopBorder & !IS_COLOR_FULLY_TRANSPARENT(topBordercolor);
    lUInt32 rightBordercolor = style->border_color[1].type != css_val_unspecified ? style->border_color[1].value : style->color.value;
    hasrightBorder = hasrightBorder & !IS_COLOR_FULLY_TRANSPARENT(rightBordercolor);
    lUInt32 bottomBordercolor = style->border_color[2].type != css_val_unspecified ? style->border_color[2].value : style->color.value;
    hasbottomBorder = hasbottomBorder & !IS_COLOR_FULLY_TRANSPARENT(bottomBordercolor);
    lUInt32 leftBordercolor = style->border_color[3].type != css_val_unspecified ? style->border_color[3].value : style->color.value;
    hasleftBorder = hasleftBorder & !IS_COLOR_FULLY_TRANSPARENT(leftBordercolor);

    if (hasbottomBorder || hasleftBorder || hasrightBorder || hastopBorder) {
        lUInt32 shadecolor=0x555555;
        lUInt32 lightcolor=0xAAAAAA;
        int width = 0; // values in % are invalid for borders, so we shouldn't get any
        int topBorderwidth = lengthToPx(enode, style->border_width[0],width);
        topBorderwidth = topBorderwidth!=0 ? topBorderwidth : DEFAULT_BORDER_WIDTH;
        int rightBorderwidth = lengthToPx(enode, style->border_width[1],width);
        rightBorderwidth = rightBorderwidth!=0 ? rightBorderwidth : DEFAULT_BORDER_WIDTH;
        int bottomBorderwidth = lengthToPx(enode, style->border_width[2],width);
        bottomBorderwidth = bottomBorderwidth!=0 ? bottomBorderwidth : DEFAULT_BORDER_WIDTH;
        int leftBorderwidth = lengthToPx(enode, style->border_width[3],width);
        leftBorderwidth = leftBorderwidth!=0 ? leftBorderwidth : DEFAULT_BORDER_WIDTH;
        int tbw=topBorderwidth,rbw=rightBorderwidth,bbw=bottomBorderwidth,lbw=leftBorderwidth;
        if (hastopBorder) {
            int dot=1,interval=0;//default style
            topBorderwidth=tbw;
            rightBorderwidth=rbw;
            // bottomBorderwidth=bbw; // (not used)
            leftBorderwidth=lbw;
            {
                lUInt32 r,g,b,o;
                r=g=b=o=topBordercolor;
                r=r>>16&0xff;
                g=g>>8&0xff;
                b=b&0xff;
                o=o&0xFF000000;
                shadecolor=o|(r*160/255)<<16|(g*160/255)<<8|b*160/255;
                lightcolor=topBordercolor;
                if ( (topBordercolor & 0xFFFFFF) == 0 ) {
                    shadecolor = o|0x4c4c4c; // Firefox uses these values when color is real black 0x000000 (but not if 0x010101)
                    lightcolor = o|0xb2b2b2;
                }
                if ( invert_colors ) {
                    topBordercolor = invertNonGrayscaleColor(topBordercolor);
                    shadecolor = invertNonGrayscaleColor(shadecolor);
                    lightcolor = invertNonGrayscaleColor(lightcolor);
                }
            }
            int left=1,right=1;
            left=(hasleftBorder)?0:1;
            right=(hasrightBorder)?0:1;
            left=(style->border_style_left==css_border_dotted||style->border_style_left==css_border_dashed)?0:left;
            right=(style->border_style_right==css_border_dotted||style->border_style_right==css_border_dashed)?0:right;
            lvPoint leftpoint1=lvPoint(x0+doc_x,y0+doc_y),
                    leftpoint2=lvPoint(x0+doc_x,y0+doc_y+0.5*topBorderwidth),
                    leftpoint3=lvPoint(x0+doc_x,doc_y+y0+topBorderwidth),
                    rightpoint1=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0),
                    rightpoint2=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0+0.5*topBorderwidth),
                    rightpoint3=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0+topBorderwidth);
            double leftrate=1,rightrate=1;
            if (left==0) {
                leftpoint1.x=x0+doc_x;
                leftpoint1.y=doc_y+y0;
                leftpoint2.x=x0+doc_x+0.5*leftBorderwidth;
                leftpoint2.y=doc_y+y0+0.5*topBorderwidth;
                leftpoint3.x=x0+doc_x+leftBorderwidth;
                leftpoint3.y=doc_y+y0+topBorderwidth;
            }else leftBorderwidth=0;
            leftrate=(double)leftBorderwidth/(double)topBorderwidth;
            if (right==0) {
                rightpoint1.x=x0+doc_x+fmt.getWidth()-1;
                rightpoint1.y=doc_y+y0;
                rightpoint2.x=x0+doc_x+fmt.getWidth()-1-0.5*rightBorderwidth;
                rightpoint2.y=doc_y+y0+0.5*topBorderwidth;
                rightpoint3.x=x0+doc_x+fmt.getWidth()-1-rightBorderwidth;
                rightpoint3.y=doc_y+y0+topBorderwidth;
            } else rightBorderwidth=0;
            rightrate=(double)rightBorderwidth/(double)topBorderwidth;
            switch (style->border_style_top){
                case css_border_dotted:
                    dot=interval=topBorderwidth;
                    for(int i=0;i<leftpoint3.y-leftpoint1.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, topBordercolor,dot,interval,0);}
                    break;
                case css_border_dashed:
                    dot=3*topBorderwidth;
                    interval=3*topBorderwidth;
                    for(int i=0;i<leftpoint3.y-leftpoint1.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, topBordercolor,dot,interval,0);}
                    break;
                case css_border_solid:
                    for(int i=0;i<leftpoint3.y-leftpoint1.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, topBordercolor,dot,interval,0);}
                    break;
                case css_border_double:
                    for(int i=0;i<=(leftpoint2.y-leftpoint1.y)/(leftpoint2.y-leftpoint1.y>2?3:2);i++)
                    {drawbuf.FillRect(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, topBordercolor);}
                    for(int i=0;i<=(leftpoint3.y-leftpoint2.y)/(leftpoint3.y-leftpoint2.y>2?3:2);i++)
                    {drawbuf.FillRect(leftpoint3.x-i*leftrate, leftpoint3.y-i, rightpoint3.x+i*rightrate+1,
                                      rightpoint3.y-i+1, topBordercolor);}
                    break;
                case css_border_groove:
                    for(int i=0;i<=leftpoint2.y-leftpoint1.y;i++)
                    {drawbuf.FillRect(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, shadecolor);}
                    for(int i=0;i<leftpoint3.y-leftpoint2.y;i++)
                    {drawbuf.FillRect(leftpoint2.x+i*leftrate, leftpoint2.y+i, rightpoint2.x-i*rightrate+1,
                                      rightpoint2.y+i+1, lightcolor);}
                    break;
                case css_border_inset:
                    for(int i=0;i<leftpoint3.y-leftpoint1.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, shadecolor,dot,interval,0);}
                    break;
                case css_border_outset:
                    for(int i=0;i<leftpoint3.y-leftpoint1.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y+i+1, lightcolor,dot,interval,0);}
                    break;
                case css_border_ridge:
                    for(int i=0;i<=leftpoint2.y-leftpoint1.y;i++)
                    {drawbuf.FillRect(leftpoint1.x+i*leftrate, leftpoint1.y+i, rightpoint1.x-i*rightrate+1,
                                     rightpoint1.y+i+1, lightcolor);}
                    for(int i=0;i<leftpoint3.y-leftpoint2.y;i++)
                    {drawbuf.FillRect(leftpoint2.x+i*leftrate, leftpoint2.y+i, rightpoint2.x-i*rightrate+1,
                                      rightpoint2.y+i+1, shadecolor);}
                    break;
                default:
                    break;
            }
        }
        //right
        if (hasrightBorder) {
            int dot=1,interval=0;//default style
            topBorderwidth=tbw;
            rightBorderwidth=rbw;
            bottomBorderwidth=bbw;
            // leftBorderwidth=lbw; // (not used)
            {
                lUInt32 r,g,b,o;
                r=g=b=o=rightBordercolor;
                r=r>>16&0xff;
                g=g>>8&0xff;
                b=b&0xff;
                o=o&0xFF000000;
                shadecolor=o|(r*160/255)<<16|(g*160/255)<<8|b*160/255;
                lightcolor=rightBordercolor;
                if ( (rightBordercolor & 0xFFFFFF) == 0 ) {
                    shadecolor = o|0x4c4c4c;
                    lightcolor = o|0xb2b2b2;
                }
                if ( invert_colors ) {
                    rightBordercolor = invertNonGrayscaleColor(rightBordercolor);
                    shadecolor = invertNonGrayscaleColor(shadecolor);
                    lightcolor = invertNonGrayscaleColor(lightcolor);
                }
            }
            int up=1,down=1;
            up=(hastopBorder)?0:1;
            down=(hasbottomBorder)?0:1;
            up=(style->border_style_top==css_border_dotted||style->border_style_top==css_border_dashed)?1:up;
            down=(style->border_style_bottom==css_border_dotted||style->border_style_bottom==css_border_dashed)?1:down;
            lvPoint toppoint1=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0),
                    toppoint2=lvPoint(x0+doc_x+fmt.getWidth()-1-0.5*rightBorderwidth,doc_y+y0),
                    toppoint3=lvPoint(x0+doc_x+fmt.getWidth()-1-rightBorderwidth,doc_y+y0),
                    bottompoint1=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0+fmt.getHeight()-1),
                    bottompoint2=lvPoint(x0+doc_x+fmt.getWidth()-1-0.5*rightBorderwidth,doc_y+y0+fmt.getHeight()-1),
                    bottompoint3=lvPoint(x0+doc_x+fmt.getWidth()-1-rightBorderwidth,doc_y+y0+fmt.getHeight()-1);
            double toprate=1,bottomrate=1;
            if (up==0) {
                toppoint3.y=doc_y+y0+topBorderwidth;
                toppoint2.y=doc_y+y0+0.5*topBorderwidth;
            } else topBorderwidth=0;
            toprate=(double)topBorderwidth/(double)rightBorderwidth;
            if (down==0) {
                bottompoint3.y=y0+doc_y+fmt.getHeight()-1-bottomBorderwidth;
                bottompoint2.y=y0+doc_y+fmt.getHeight()-1-0.5*bottomBorderwidth;
            } else bottomBorderwidth=0;
            bottomrate=(double)bottomBorderwidth/(double)rightBorderwidth;
            switch (style->border_style_right){
                case css_border_dotted:
                    dot=interval=rightBorderwidth;
                    for (int i=0;i<toppoint1.x-toppoint3.x;i++){
                        drawbuf.DrawLine(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, rightBordercolor,dot,interval,1);
                    }
                    break;
                case css_border_dashed:
                    dot=3*rightBorderwidth;
                    interval=3*rightBorderwidth;
                    for (int i=0;i<toppoint1.x-toppoint3.x;i++){
                        drawbuf.DrawLine(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, rightBordercolor,dot,interval,1);
                    }
                    break;
                case css_border_solid:
                    for (int i=0;i<toppoint1.x-toppoint3.x;i++){
                        drawbuf.DrawLine(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, rightBordercolor,dot,interval,1);
                    }
                    break;
                case css_border_double:
                    for (int i=0;i<=(toppoint1.x-toppoint2.x)/(toppoint1.x-toppoint2.x>2?3:2);i++){
                        drawbuf.FillRect(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, rightBordercolor);
                    }
                    for (int i=0;i<=(toppoint2.x-toppoint3.x)/(toppoint2.x-toppoint3.x>2?3:2);i++){
                        drawbuf.FillRect(toppoint3.x+i,toppoint3.y-i*toprate,bottompoint3.x+i+1,
                                         bottompoint3.y+i*bottomrate+1, rightBordercolor);
                    }
                    break;
                case css_border_groove:
                    for (int i=0;i<toppoint1.x-toppoint2.x;i++){
                        drawbuf.FillRect(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, lightcolor);
                    }
                    for (int i=0;i<=toppoint2.x-toppoint3.x;i++){
                        drawbuf.FillRect(toppoint2.x-i,toppoint2.y+i*toprate,bottompoint2.x-i+1,
                                         bottompoint2.y-i*bottomrate+1, shadecolor);
                    }
                    break;
                case css_border_inset:
                    for (int i=0;i<toppoint1.x-toppoint3.x;i++){
                        drawbuf.DrawLine(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, lightcolor,dot,interval,1);
                    }
                    break;
                case css_border_outset:
                    for (int i=0;i<toppoint1.x-toppoint3.x;i++){
                        drawbuf.DrawLine(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, shadecolor,dot,interval,1);
                    }
                    break;
                case css_border_ridge:
                    for (int i=0;i<toppoint1.x-toppoint2.x;i++){
                        drawbuf.FillRect(toppoint1.x-i,toppoint1.y+i*toprate,bottompoint1.x-i+1,
                                         bottompoint1.y-i*bottomrate+1, shadecolor);
                    }
                    for (int i=0;i<=toppoint2.x-toppoint3.x;i++){
                        drawbuf.FillRect(toppoint2.x-i,toppoint2.y+i*toprate,bottompoint2.x-i+1,
                                         bottompoint2.y-i*bottomrate+1,lightcolor);
                    }
                    break;
                default:break;
            }
        }
        //bottom
        if (hasbottomBorder) {
            int dot=1,interval=0;//default style
            // topBorderwidth=tbw; // (not used)
            rightBorderwidth=rbw;
            bottomBorderwidth=bbw;
            leftBorderwidth=lbw;
            {
                lUInt32 r,g,b,o;
                r=g=b=o=bottomBordercolor;
                r=r>>16&0xff;
                g=g>>8&0xff;
                b=b&0xff;
                o=o&0xFF000000;
                shadecolor=o|(r*160/255)<<16|(g*160/255)<<8|b*160/255;
                lightcolor=bottomBordercolor;
                if ( (bottomBordercolor & 0xFFFFFF) == 0 ) {
                    shadecolor = o|0x4c4c4c;
                    lightcolor = o|0xb2b2b2;
                }
                if ( invert_colors ) {
                    bottomBordercolor = invertNonGrayscaleColor(bottomBordercolor);
                    shadecolor = invertNonGrayscaleColor(shadecolor);
                    lightcolor = invertNonGrayscaleColor(lightcolor);
                }
            }
            int left=1,right=1;
            left=(hasleftBorder)?0:1;
            right=(hasrightBorder)?0:1;
            left=(style->border_style_left==css_border_dotted||style->border_style_left==css_border_dashed)?1:left;
            right=(style->border_style_right==css_border_dotted||style->border_style_right==css_border_dashed)?1:right;
            lvPoint leftpoint1=lvPoint(x0+doc_x,y0+doc_y+fmt.getHeight()-1),
                    leftpoint2=lvPoint(x0+doc_x,y0+doc_y-0.5*bottomBorderwidth+fmt.getHeight()-1),
                    leftpoint3=lvPoint(x0+doc_x,doc_y+y0+fmt.getHeight()-1-bottomBorderwidth),
                    rightpoint1=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0+fmt.getHeight()-1),
                    rightpoint2=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0+fmt.getHeight()-1-0.5*bottomBorderwidth),
                    rightpoint3=lvPoint(x0+doc_x+fmt.getWidth()-1,doc_y+y0+fmt.getHeight()-1-bottomBorderwidth);
            double leftrate=1,rightrate=1;
            if (left==0) {
                leftpoint3.x=x0+doc_x+leftBorderwidth;
                leftpoint2.x=x0+doc_x+0.5*leftBorderwidth;
            }else leftBorderwidth=0;
            leftrate=(double)leftBorderwidth/(double)bottomBorderwidth;
            if (right==0) {
                rightpoint3.x=x0+doc_x+fmt.getWidth()-1-rightBorderwidth;
                rightpoint2.x=x0+doc_x+fmt.getWidth()-1-0.5*rightBorderwidth;
            } else rightBorderwidth=0;
            rightrate=(double)rightBorderwidth/(double)bottomBorderwidth;
            switch (style->border_style_bottom){
                case css_border_dotted:
                    dot=interval=bottomBorderwidth;
                    for(int i=0;i<leftpoint1.y-leftpoint3.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, bottomBordercolor,dot,interval,0);}
                    break;
                case css_border_dashed:
                    dot=3*bottomBorderwidth;
                    interval=3*bottomBorderwidth;
                    for(int i=0;i<leftpoint1.y-leftpoint3.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, bottomBordercolor,dot,interval,0);}
                    break;
                case css_border_solid:
                    for(int i=0;i<leftpoint1.y-leftpoint3.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, bottomBordercolor,dot,interval,0);}
                    break;
                case css_border_double:
                    for(int i=0;i<=(leftpoint1.y-leftpoint2.y)/(leftpoint1.y-leftpoint2.y>2?3:2);i++)
                    {drawbuf.FillRect(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, bottomBordercolor);}
                    for(int i=0;i<=(leftpoint2.y-leftpoint3.y)/(leftpoint2.y-leftpoint3.y>2?3:2);i++)
                    {drawbuf.FillRect(leftpoint3.x-i*leftrate, leftpoint3.y+i, rightpoint3.x+i*rightrate+1,
                                      rightpoint3.y+i+1, bottomBordercolor);}
                    break;
                case css_border_groove:
                    for(int i=0;i<=leftpoint1.y-leftpoint2.y;i++)
                    {drawbuf.FillRect(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, lightcolor);}
                    for(int i=0;i<leftpoint2.y-leftpoint3.y;i++)
                    {drawbuf.FillRect(leftpoint2.x+i*leftrate, leftpoint2.y-i, rightpoint2.x-i*rightrate+1,
                                      rightpoint2.y-i+1, shadecolor);}
                    break;
                case css_border_inset:
                    for(int i=0;i<leftpoint1.y-leftpoint3.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, lightcolor,dot,interval,0);}
                    break;
                case css_border_outset:
                    for(int i=0;i<leftpoint1.y-leftpoint3.y;i++)
                    {drawbuf.DrawLine(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, shadecolor,dot,interval,0);}
                    break;
                case css_border_ridge:
                    for(int i=0;i<=leftpoint1.y-leftpoint2.y;i++)
                    {drawbuf.FillRect(leftpoint1.x+i*leftrate, leftpoint1.y-i, rightpoint1.x-i*rightrate+1,
                                      rightpoint1.y-i+1, shadecolor);}
                    for(int i=0;i<leftpoint2.y-leftpoint3.y;i++)
                    {drawbuf.FillRect(leftpoint2.x+i*leftrate, leftpoint2.y-i, rightpoint2.x-i*rightrate+1,
                                      rightpoint2.y-i+1, lightcolor);}
                    break;
                default:break;
            }
        }
        //left
        if (hasleftBorder) {
            int dot=1,interval=0;//default style
            topBorderwidth=tbw;
            // rightBorderwidth=rbw; // (not used)
            bottomBorderwidth=bbw;
            leftBorderwidth=lbw;
            {
                lUInt32 r,g,b,o;
                r=g=b=o=leftBordercolor;
                r=r>>16&0xff;
                g=g>>8&0xff;
                b=b&0xff;
                o=o&0xFF000000;
                shadecolor=o|(r*160/255)<<16|(g*160/255)<<8|b*160/255;
                lightcolor=leftBordercolor;
                if ( (leftBordercolor & 0xFFFFFF) == 0 ) {
                    shadecolor = o|0x4c4c4c;
                    lightcolor = o|0xb2b2b2;
                }
                if ( invert_colors ) {
                    leftBordercolor = invertNonGrayscaleColor(leftBordercolor);
                    shadecolor = invertNonGrayscaleColor(shadecolor);
                    lightcolor = invertNonGrayscaleColor(lightcolor);
                }
            }
            int up=1,down=1;
            up=(hastopBorder)?0:1;
            down=(hasbottomBorder)?0:1;
            up=(style->border_style_top==css_border_dotted||style->border_style_top==css_border_dashed)?1:up;
            down=(style->border_style_bottom==css_border_dotted||style->border_style_bottom==css_border_dashed)?1:down;
            lvPoint toppoint1=lvPoint(x0+doc_x,doc_y+y0),
                    toppoint2=lvPoint(x0+doc_x+0.5*leftBorderwidth,doc_y+y0),
                    toppoint3=lvPoint(x0+doc_x+leftBorderwidth,doc_y+y0),
                    bottompoint1=lvPoint(x0+doc_x,doc_y+y0+fmt.getHeight()-1),
                    bottompoint2=lvPoint(x0+doc_x+0.5*leftBorderwidth,doc_y+y0+fmt.getHeight()-1),
                    bottompoint3=lvPoint(x0+doc_x+leftBorderwidth,doc_y+y0+fmt.getHeight()-1);
            double toprate=1,bottomrate=1;
            if (up==0) {
                toppoint3.y=doc_y+y0+topBorderwidth;
                toppoint2.y=doc_y+y0+0.5*topBorderwidth;
            } else topBorderwidth=0;
            toprate=(double)topBorderwidth/(double)leftBorderwidth;
            if (down==0) {
                bottompoint3.y=y0+doc_y+fmt.getHeight()-1-bottomBorderwidth;
                bottompoint2.y=y0+doc_y+fmt.getHeight()-1-0.5*bottomBorderwidth;
            } else bottomBorderwidth=0;
            bottomrate=(double)bottomBorderwidth/(double)leftBorderwidth;
            switch (style->border_style_left){
                case css_border_dotted:
                    dot=interval=leftBorderwidth;
                    for (int i=0;i<toppoint3.x-toppoint1.x;i++){
                        drawbuf.DrawLine(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,leftBordercolor,dot,interval,1);
                    }
                    break;
                case css_border_dashed:
                    dot=3*leftBorderwidth;
                    interval=3*leftBorderwidth;
                    for (int i=0;i<toppoint3.x-toppoint1.x;i++){
                        drawbuf.DrawLine(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,leftBordercolor,dot,interval,1);
                    }
                    break;
                case css_border_solid:
                    for (int i=0;i<toppoint3.x-toppoint1.x;i++){
                        drawbuf.DrawLine(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,leftBordercolor,dot,interval,1);
                    }
                    break;
                case css_border_double:
                    for (int i=0;i<=(toppoint2.x-toppoint1.x)/(toppoint2.x-toppoint1.x>2?3:2);i++){
                        drawbuf.FillRect(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,leftBordercolor);
                    }
                    for (int i=0;i<=(toppoint3.x-toppoint2.x)/(toppoint3.x-toppoint2.x>2?3:2);i++){
                        drawbuf.FillRect(toppoint3.x-i,toppoint3.y-i*toprate,bottompoint3.x-i+1,
                                         bottompoint3.y+i*bottomrate+1,leftBordercolor);
                    }
                    break;
                case css_border_groove:
                    for (int i=0;i<=toppoint2.x-toppoint1.x;i++){
                        drawbuf.FillRect(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,shadecolor);
                    }
                    for (int i=0;i<toppoint3.x-toppoint2.x;i++){
                        drawbuf.FillRect(toppoint2.x+i,toppoint2.y+i*toprate,bottompoint2.x+i+1,
                                         bottompoint2.y-i*bottomrate+1,lightcolor);
                    }
                    break;
                case css_border_inset:
                    for (int i=0;i<toppoint3.x-toppoint1.x;i++){
                        drawbuf.DrawLine(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,shadecolor,dot,interval,1);
                    }
                    break;
                case css_border_outset:
                    for (int i=0;i<toppoint3.x-toppoint1.x;i++){
                        drawbuf.DrawLine(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,lightcolor,dot,interval,1);
                    }
                    break;
                case css_border_ridge:
                    for (int i=0;i<=toppoint2.x-toppoint1.x;i++){
                        drawbuf.FillRect(toppoint1.x+i,toppoint1.y+i*toprate,bottompoint1.x+i+1,
                                         bottompoint1.y-i*bottomrate+1,lightcolor);
                    }
                    for (int i=0;i<toppoint3.x-toppoint2.x;i++){
                        drawbuf.FillRect(toppoint2.x+i,toppoint2.y+i*toprate,bottompoint2.x+i+1,
                                         bottompoint2.y-i*bottomrate+1,shadecolor);
                    }
                    break;
                default:break;
            }
        }
    }
}

void DrawBackgroundImage(ldomNode *enode,LVDrawBuf & drawbuf,int x0,int y0,int doc_x,int doc_y, int width, int height, bool clip_to_target)
{
    // The caller passes us the node's border box (fmt.getWidth()/getHeight()), for
    // background-color's default background-clip: border-box. But background-position/-size
    // resolve against the padding box (the default background-origin), so inset by the
    // border width below -- see https://www.w3.org/TR/css-backgrounds-3/#the-background-origin
    css_style_ref_t style=enode->getStyle();
    if (!style->background_image.empty()) {
        int leftBorderwidth = measureBorder(enode, 3);
        int topBorderwidth = measureBorder(enode, 0);
        int rightBorderwidth = measureBorder(enode, 1);
        int bottomBorderwidth = measureBorder(enode, 2);
        if (leftBorderwidth || topBorderwidth || rightBorderwidth || bottomBorderwidth) {
            x0 += leftBorderwidth;
            y0 += topBorderwidth;
            width -= leftBorderwidth + rightBorderwidth;
            height -= topBorderwidth + bottomBorderwidth;
        }
        lString32 filepath = lString32(style->background_image.c_str());
        LVImageSourceRef img = enode->getParentNode()->getDocument()->getObjectImageSource(filepath);
        if (img.isNull()) { // filepath may be url-encoded
            img = enode->getParentNode()->getDocument()->getObjectImageSource(DecodeHTMLUrlString(filepath));
        }
        if (!img.isNull() && width > 0 && height > 0) {
            // Raw, undecoded-transform pixel size of the image file
            int native_img_w = img->GetWidth();
            int native_img_h = img->GetHeight();
            // Native image size, scaled according to gRenderDPI like getStyledImageSize()
            // does for <img> elements.
            int img_w = scaleForRenderDPI(native_img_w);
            int img_h = scaleForRenderDPI(native_img_h);

            // See if background-size specified and we need to adjust image native size
            // (if both auto, use image native size)
            css_length_t bg_w = style->background_size[0];
            css_length_t bg_h = style->background_size[1];
            if ( bg_w.type != css_val_unspecified || bg_w.value != css_generic_auto ||
                 bg_h.type != css_val_unspecified || bg_h.value != css_generic_auto ) {
                int new_w = 0;
                int new_h = 0;
                // Use the (already border-inset) padding box as the basis for percentage
                // sizes and for cover/contain scaling.
                int container_w = width;
                int container_h = height;
                bool check_lengths = true;
                if ( bg_w.type == css_val_unspecified && bg_h.type == css_val_unspecified ) {
                    if ( bg_w.value == css_generic_contain && bg_h.value == css_generic_contain ) {
                        // Image should be fully contained in container (no crop)
                        int scale_w = 1024 * container_w / img_w;
                        int scale_h = 1024 * container_h / img_h;
                        if ( scale_w < scale_h ) {
                            new_w = container_w;
                            new_h = img_h * scale_w / 1024;
                        }
                        else {
                            new_h = container_h;
                            new_w = img_w * scale_h / 1024;
                        }
                        check_lengths = false;
                    }
                    else if ( bg_w.value == css_generic_cover && bg_h.value == css_generic_cover ) {
                        // Image should fully cover container (crop allowed)
                        int scale_w = 1024 * container_w / img_w;
                        int scale_h = 1024 * container_h / img_h;
                        if ( scale_w > scale_h ) {
                            new_w = container_w;
                            new_h = img_h * scale_w / 1024;
                        }
                        else {
                            new_h = container_h;
                            new_w = img_w * scale_h / 1024;
                        }
                        check_lengths = false;
                    }
                }
                if ( check_lengths ) {
                    // These will compute to 0 if (css_val_unspecified, css_generic_auto) when really not specified
                    new_w = lengthToPx(enode, style->background_size[0], container_w);
                    new_h = lengthToPx(enode, style->background_size[1], container_h);
                    if ( new_w == 0 ) {
                        if ( new_h == 0 ) { // keep image native size
                            new_h = img_h;
                            new_w = img_w;
                        }
                        else { // use style height, keep aspect ratio
                            new_w = img_w * new_h / img_h;
                        }
                    }
                    else if ( new_h == 0 ) { // use style width, keep aspect ratio
                        new_h = new_w * img_h / img_w;
                    }
                }
                if ( new_w == 0 || new_h == 0 ) {
                    // width or height computed to 0: nothing to draw
                    return;
                }
                img_w = new_w;
                img_h = new_h;
            }
            // Resize the decoded image to img_w x img_h if that doesn't match its
            // native pixel size, whether because of background-size, of gRenderDPI
            // scaling, or both (img_w/img_h above already account for either).
            // Honor the same "Image Scaling" (smooth vs nearest-neighbor) setting
            // used for normal <img> elements, so background-image scaling looks
            // consistent with the rest of the page.
            if ( img_w != native_img_w || img_h != native_img_h ) {
                img = LVCreateStretchFilledTransform(img, img_w, img_h, IMG_TRANSFORM_STRETCH, IMG_TRANSFORM_STRETCH, 0, 0,
                                                      drawbuf.getSmoothScalingImages());
            }

            // We can use some crengine facilities for background repetition and position,
            // which has the advantage that img will be decoded once even if tiling it many
            // times and if the target is many screen-heights long (like <BODY> could be).
            // Unfortunaly, it does not everything well when not using IMG_TRANSFORM_TILE,
            // as it would fill the not-drawn part of the target buffer with garbage,
            // instead of letting it as is.
            ImageTransform hori_transform = IMG_TRANSFORM_NONE;
            ImageTransform vert_transform = IMG_TRANSFORM_NONE;
            int transform_w = img_w;
            int transform_h = img_h;
            switch (style->background_repeat) {
                case css_background_no_repeat:
                case css_background_repeat_y:
                    break;
                case css_background_repeat_x:
                case css_background_repeat:
                default:
                    // No need to tile if image is larger than target
                    if ( width > img_w ) {
                        hori_transform = IMG_TRANSFORM_TILE;
                        transform_w = width;
                    }
                    break;
            }
            switch (style->background_repeat) {
                case css_background_no_repeat:
                case css_background_repeat_x:
                    break;
                case css_background_repeat_y:
                case css_background_repeat:
                default:
                    // No need to tile if image is larger than target
                    if ( height > img_h ) {
                        vert_transform = IMG_TRANSFORM_TILE;
                        transform_h = height;
                    }
                    break;
            }
            // Compute the position where to draw top left of image, as if
            // it was a single image when no-repeat.
            // Per spec, a <percentage> position is relative to the difference
            // between the container and (possibly background-size resized)
            // image sizes, while a <length> is a plain absolute offset.
            css_length_t bg_pos_x = style->background_position[0];
            css_length_t bg_pos_y = style->background_position[1];
            int draw_x;
            if ( bg_pos_x.type == css_val_percent )
                draw_x = (width - img_w) * bg_pos_x.value / (100 * 256);
            else
                draw_x = lengthToPx(enode, bg_pos_x, width);
            int draw_y;
            if ( bg_pos_y.type == css_val_percent )
                draw_y = (height - img_h) * bg_pos_y.value / (100 * 256);
            else
                draw_y = lengthToPx(enode, bg_pos_y, height);
            // If tiling, we need to adjust the transform x/y (the offset
            // in img, so, a value between 0 and img_w/h) to the point
            // inside image that should be at top left of target area
            int transform_x = 0;
            int transform_y = 0;
            if ( hori_transform == IMG_TRANSFORM_TILE && draw_x ) {
                transform_x = (draw_x % img_w);
                draw_x = 0;
            }
            if ( vert_transform == IMG_TRANSFORM_TILE && draw_y ) {
                // Strangely, using the following instead of what we did for x/w
                // gives the expected result (not investigated, might be
                // a bug in LVStretchImgSource::OnLineDecoded() )
                transform_y = img_h - (draw_y % img_h);
                draw_y = 0;
            }
            // Ready to have crengine do all the work.
            /* Looks like we don't need that:

                // (Inspired from LVDocView::drawPageBackground(),
                // we have to do it the complex way to avoid memory leaks
                LVRef<LVColorDrawBuf> buf = LVRef<LVColorDrawBuf>( new LVColorDrawBuf(img_w, img_h, 32) );
                buf->Draw(img, 0, 0, img_w, img_h, false); // (dither=false doesn't matter with a color buffer)
                LVImageSourceRef src = LVCreateDrawBufImageSource(buf.get(), false);
                LVImageSourceRef transformed = LVCreateStretchFilledTransform(src, transform_w, transform_h,

              We can just transform the original image, which will work in its original
              colorspace/depth, ensure alpha/transparency, and will be converted only
              at the end to the final drawbuf bit depth.
            */
            LVImageSourceRef transformed = LVCreateStretchFilledTransform(img, transform_w, transform_h,
                                               hori_transform, vert_transform, transform_x, transform_y);
            // We use the DrawBuf clip facility to ensure we don't draw outside this node fmt
            lvRect orig_clip;
            if (clip_to_target) {
                drawbuf.GetClipRect( &orig_clip ); // Backup the original one
                // Set a new one to the target area
                lvRect target_clip = lvRect(x0+doc_x, y0+doc_y, x0+doc_x+width, y0+doc_y+height);;
                // But don't overflow page top and bottom, in case target spans multiple pages
                if ( target_clip.top < orig_clip.top )
                    target_clip.top = orig_clip.top;
                if ( target_clip.bottom > orig_clip.bottom )
                    target_clip.bottom = orig_clip.bottom;
                drawbuf.SetClipRect( &target_clip );
            }
            // Draw
            drawbuf.Draw(transformed, x0+doc_x+draw_x, y0+doc_y+draw_y, transform_w, transform_h);
            if (clip_to_target) {
                drawbuf.SetClipRect( &orig_clip ); // Restore the original one
            }
        }
    }
}

void DrawBodyBackground( LVDrawBuf & drawbuf, bool draw_bg_color, bool draw_bg_image, ldomNode * enode, int x0, int y0, int dx, int dy, int doc_x, int doc_y)
{
    // https://www.w3.org/TR/CSS2/colors.html#background
    // <body> background does not obey margin rules, and it is to be drawn
    // instead on the whole canvas/viewport.
    // This is rather complex with EPUBs and DocFragment based documents,
    // as there are multiple BODYs that are usually split on new pages,
    // but could also meet on a page.
    // We don't draw on the fmt width, but on the drawbuf width.
    // Also, when in page mode, we'd rather have a fully fixed background,
    // (so, not respecting background-repeat and background-position)
    // to avoid ghosting and refreshes issues on eInk.
    // We try to do this right when there are multiple <BODY>, with possibly
    // different background colors/images, in the viewed page. This is a bit
    // harder to do right when in 2-pages mode, which can have a few issues.

    // We can draw on the whole buffer or clip area, unless some previous
    // or next body restrict these
    int bg_top = 0;
    int bg_bottom = drawbuf.GetHeight();
    int bg_left = 0;
    int bg_right = drawbuf.GetWidth();

    // Use the specific body background clip so the background is drawn
    // on the full canvas even on pages with shorter text.
    lvRect curclip;
    drawbuf.GetClipRect( &curclip );
    draw_extra_info_t * draw_extra_info = (draw_extra_info_t*)drawbuf.GetDrawExtraInfo();
    if ( draw_extra_info ) {
        // Set body background clip (we get one if in page mode)
        drawbuf.SetClipRect( &draw_extra_info->body_background_clip );
        // If there is a header or we are in 2-pages mode, the clip would ensure
        // we don't draw over them. But we want to position the drawing
        // adequately so the background-position can be ensured;
        // just use the provided clip as the area to paint
        bg_top = draw_extra_info->body_background_clip.top;
        bg_bottom = draw_extra_info->body_background_clip.bottom;
        bg_left = draw_extra_info->body_background_clip.left;
        bg_right = draw_extra_info->body_background_clip.right;
    }

    // If the current body we're dealing starts on ends inside this page/screen,
    // it does not necessarily mean there is a previous or next body that ends
    // or starts inside this page/screen: we may have inter body margins, or
    // some initial top margin above the first body.
    // We need to check there is really none to be able to draw on the whole buffer.
    const bool no_visible_previous_body = doc_y <= 0; // This body started before page top
    if ( !no_visible_previous_body ) {
        // Find previous body if any, to see if would have some part in this page
        // We expect either sibling BODY (FB2) or sibling DocFragement>BODY (EPUB)
        ldomNode * prevBody = NULL;
        ldomNode * n;
        n = enode->getUnboxedPrevSibling(true);
        if ( n && n->getNodeId() == el_body ) {
            prevBody = n;
        }
        else {
            n = enode->getUnboxedParent();
            if ( n && n->getNodeId() == el_DocFragment ) {
                n = n->getUnboxedPrevSibling(true);
                if ( n && n->getNodeId() == el_DocFragment ) {
                    n = n->getUnboxedLastChild(true);
                    if ( n && n->getNodeId() == el_body ) {
                        prevBody = n;
                    }
                }
            }
        }
        if ( prevBody ) {
            // Make out the doc_y this prev body would have
            lvRect prevrect;
            prevBody->getAbsRect(prevrect);
            lvRect thisrect;
            enode->getAbsRect(thisrect);
            int prev_bottom_doc_y = doc_y - thisrect.top + prevrect.bottom;
            if ( prev_bottom_doc_y > 0 ) {
                // Previous body does not end before this page: there may be unused
                // space between this prev body bottom and this body top, caused by
                // collapsed body top/bottom margins.
                // Make the boundary between backgrounds at the middle of this (round up)
                bg_top = y0 + doc_y - (thisrect.top - prevrect.bottom)/2;
            }
        }
    }
    // Same checks as above, but for a next body below this one
    RenderRectAccessor fmt( enode );
    const bool no_visible_next_body = doc_y + fmt.getHeight() >= dy; // this body ends after page bottom
    if ( !no_visible_next_body ) {
        // Find next body
        ldomNode * nextBody = NULL;
        ldomNode * n;
        n = enode->getUnboxedNextSibling(true);
        if ( n && n->getNodeId() == el_body ) {
            nextBody = n;
        }
        else {
            n = enode->getUnboxedParent();
            if ( n && n->getNodeId() == el_DocFragment ) {
                n = n->getUnboxedNextSibling(true);
                if ( n && n->getNodeId() == el_DocFragment ) {
                    n = n->getUnboxedLastChild(true); // body can be preceded by <styleSheet>
                    if ( n && n->getNodeId() == el_body ) {
                        nextBody = n;
                    }
                }
            }
        }
        if ( nextBody ) {
            lvRect nextrect;
            nextBody->getAbsRect(nextrect);
            lvRect thisrect;
            enode->getAbsRect(thisrect);
            int next_top_doc_y = doc_y - thisrect.top + nextrect.top;
            if ( next_top_doc_y < dy ) {
                // Next body starts on this page: There may be unused space
                // between this next body top and this body bottom, caused by
                // collapsed body top/bottom margins.
                // Make the boundary between backgrounds at the middle of this (round down)
                bg_bottom = y0 + doc_y + fmt.getHeight() + (nextrect.top - thisrect.bottom + 1)/2;
            }
        }
    }

    if ( draw_bg_color ) {
        css_style_ref_t style = enode->getStyle();
        // If not css_val_color, it must be (css_val_unspecified, css_generic_currentcolor)
        lUInt32 bg_color = style->background_color.type == css_val_color ? style->background_color.value : style->color.value;
        bg_color = drawbuf.getInvertColors() ? invertNonGrayscaleColor(bg_color) : bg_color;
        drawbuf.FillRect(bg_left, bg_top, bg_right, bg_bottom, bg_color);
    }
    if ( draw_bg_image ) {
        // We will provide clip_to_target=false to DrawBackgroundImage() for it to not
        // limit the clip to the body boundaries, which could give unexpected results
        // and is tricky to visualize how it would behave in all cases... It's easier
        // to just adjust the clip to limit the painted area.
        lvRect clip;
        drawbuf.GetClipRect( &clip );
        // We got either the orig fullscreen clip in scroll mode, or body_background_clip
        // in page mode, which have proper clip left and right.
        if ( clip.top < bg_top )
            clip.top = bg_top;
        if ( clip.bottom > bg_bottom )
            clip.bottom = bg_bottom;
        drawbuf.SetClipRect(&clip);
        // We provide x=0 w=screen width so that even if the clip crops out half of
        // this width, we get the background image positionned (background-position)
        // and repeated (backgroud-repeat) the same way whether we're drawing the
        // left of the right page: that way, drawings will coincide and look like a
        // single full page drawing, instead of having a cut in the middle.
        // We provide y=bg_top, so that when the body starts in the middle of the
        // page, the image have its top where it starts, as this might matter for
        // some images.
        DrawBackgroundImage(enode, drawbuf, 0, bg_top, 0, 0, drawbuf.GetWidth(), drawbuf.GetHeight()-bg_top, false);
    }

    drawbuf.SetClipRect(&curclip); // restore clip
}
