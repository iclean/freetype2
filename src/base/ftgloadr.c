#include <ft2build.h>
#include FT_INTERNAL_GLYPH_LOADER_H
#include FT_INTERNAL_MEMORY_H

#undef  FT_COMPONENT
#define FT_COMPONENT  trace_gloader

  /*************************************************************************/
  /*************************************************************************/
  /*************************************************************************/
  /*****                                                               *****/
  /*****                                                               *****/
  /*****                    G L Y P H   L O A D E R                    *****/
  /*****                                                               *****/
  /*****                                                               *****/
  /*************************************************************************/
  /*************************************************************************/
  /*************************************************************************/

  /*************************************************************************/
  /*                                                                       */
  /* The glyph loader is a simple object which is used to load a set of    */
  /* glyphs easily.  It is critical for the correct loading of composites. */
  /*                                                                       */
  /* Ideally, one can see it as a stack of abstract `glyph' objects.       */
  /*                                                                       */
  /*   loader.base     Is really the bottom of the stack.  It describes a  */
  /*                   single glyph image made of the juxtaposition of     */
  /*                   several glyphs (those `in the stack').              */
  /*                                                                       */
  /*   loader.current  Describes the top of the stack, on which a new      */
  /*                   glyph can be loaded.                                */
  /*                                                                       */
  /*   Rewind          Clears the stack.                                   */
  /*   Prepare         Set up `loader.current' for addition of a new glyph */
  /*                   image.                                              */
  /*   Add             Add the `current' glyph image to the `base' one,    */
  /*                   and prepare for another one.                        */
  /*                                                                       */
  /* The glyph loader is now a base object.  Each driver used to           */
  /* re-implement it in one way or the other, which wasted code and        */
  /* energy.                                                               */
  /*                                                                       */
  /*************************************************************************/


  /* create a new glyph loader */
  FT_BASE_DEF( FT_Error )
  FT_GlyphLoader_New( FT_Memory         memory,
                      FT_GlyphLoader   *aloader )
  {
    FT_GlyphLoader   loader;
    FT_Error         error;


    if ( !ALLOC( loader, sizeof ( *loader ) ) )
    {
      loader->memory = memory;
      *aloader       = loader;
    }
    return error;
  }


  /* rewind the glyph loader - reset counters to 0 */
  FT_BASE_DEF( void )
  FT_GlyphLoader_Rewind( FT_GlyphLoader   loader )
  {
    FT_GlyphLoad  base    = &loader->base;
    FT_GlyphLoad  current = &loader->current;


    base->outline.n_points   = 0;
    base->outline.n_contours = 0;
    base->num_subglyphs      = 0;

    *current = *base;
  }


  /* reset the glyph loader, frees all allocated tables */
  /* and starts from zero                               */
  FT_BASE_DEF( void )
  FT_GlyphLoader_Reset( FT_GlyphLoader   loader )
  {
    FT_Memory memory = loader->memory;


    FREE( loader->base.outline.points );
    FREE( loader->base.outline.tags );
    FREE( loader->base.outline.contours );
    FREE( loader->base.extra_points );
    FREE( loader->base.subglyphs );

    loader->max_points    = 0;
    loader->max_contours  = 0;
    loader->max_subglyphs = 0;

    FT_GlyphLoader_Rewind( loader );
  }


  /* delete a glyph loader */
  FT_BASE_DEF( void )
  FT_GlyphLoader_Done( FT_GlyphLoader   loader )
  {
    if ( loader )
    {
      FT_Memory memory = loader->memory;


      FT_GlyphLoader_Reset( loader );
      FREE( loader );
    }
  }


  /* re-adjust the `current' outline fields */
  static void
  FT_GlyphLoader_Adjust_Points( FT_GlyphLoader   loader )
  {
    FT_Outline*  base    = &loader->base.outline;
    FT_Outline*  current = &loader->current.outline;


    current->points   = base->points   + base->n_points;
    current->tags     = base->tags     + base->n_points;
    current->contours = base->contours + base->n_contours;

    /* handle extra points table - if any */
    if ( loader->use_extra )
      loader->current.extra_points =
        loader->base.extra_points + base->n_points;
  }


  FT_BASE_DEF( FT_Error )
  FT_GlyphLoader_Create_Extra( FT_GlyphLoader   loader )
  {
    FT_Error   error;
    FT_Memory  memory = loader->memory;


    if ( !ALLOC_ARRAY( loader->base.extra_points,
                       loader->max_points, FT_Vector ) )
    {
      loader->use_extra = 1;
      FT_GlyphLoader_Adjust_Points( loader );
    }
    return error;
  }


  /* re-adjust the `current' subglyphs field */
  static void
  FT_GlyphLoader_Adjust_Subglyphs( FT_GlyphLoader   loader )
  {
    FT_GlyphLoad  base    = &loader->base;
    FT_GlyphLoad  current = &loader->current;


    current->subglyphs = base->subglyphs + base->num_subglyphs;
  }


  /* Ensure that we can add `n_points' and `n_contours' to our glyph. this */
  /* function reallocates its outline tables if necessary.  Note that it   */
  /* DOESN'T change the number of points within the loader!                */
  /*                                                                       */
  FT_BASE_DEF( FT_Error )
  FT_GlyphLoader_Check_Points( FT_GlyphLoader   loader,
                               FT_UInt          n_points,
                               FT_UInt          n_contours )
  {
    FT_Memory    memory  = loader->memory;
    FT_Error     error   = FT_Err_Ok;
    FT_Outline*  base    = &loader->base.outline;
    FT_Outline*  current = &loader->current.outline;
    FT_Bool      adjust  = 1;

    FT_UInt      new_max, old_max;


    /* check points & tags */
    new_max = base->n_points + current->n_points + n_points;
    old_max = loader->max_points;

    if ( new_max > old_max )
    {
      new_max = ( new_max + 7 ) & -8;

      if ( REALLOC_ARRAY( base->points, old_max, new_max, FT_Vector ) ||
           REALLOC_ARRAY( base->tags,   old_max, new_max, FT_Byte   ) )
       goto Exit;

      if ( loader->use_extra &&
           REALLOC_ARRAY( loader->base.extra_points, old_max,
                          new_max, FT_Vector ) )
       goto Exit;

      adjust = 1;
      loader->max_points = new_max;
    }

    /* check contours */
    old_max = loader->max_contours;
    new_max = base->n_contours + current->n_contours +
              n_contours;
    if ( new_max > old_max )
    {
      new_max = ( new_max + 3 ) & -4;
      if ( REALLOC_ARRAY( base->contours, old_max, new_max, FT_Short ) )
        goto Exit;

      adjust = 1;
      loader->max_contours = new_max;
    }

    if ( adjust )
      FT_GlyphLoader_Adjust_Points( loader );

  Exit:
    return error;
  }


  /* Ensure that we can add `n_subglyphs' to our glyph. this function */
  /* reallocates its subglyphs table if necessary.  Note that it DOES */
  /* NOT change the number of subglyphs within the loader!            */
  /*                                                                  */
  FT_BASE_DEF( FT_Error )
  FT_GlyphLoader_Check_Subglyphs( FT_GlyphLoader   loader,
                                  FT_UInt          n_subs )
  {
    FT_Memory  memory = loader->memory;
    FT_Error   error  = FT_Err_Ok;
    FT_UInt    new_max, old_max;

    FT_GlyphLoad  base    = &loader->base;
    FT_GlyphLoad  current = &loader->current;


    new_max = base->num_subglyphs + current->num_subglyphs + n_subs;
    old_max = loader->max_subglyphs;
    if ( new_max > old_max )
    {
      new_max = ( new_max + 1 ) & -2;
      if ( REALLOC_ARRAY( base->subglyphs, old_max, new_max, FT_SubGlyph ) )
        goto Exit;

      loader->max_subglyphs = new_max;

      FT_GlyphLoader_Adjust_Subglyphs( loader );
    }

  Exit:
    return error;
  }


  /* prepare loader for the addition of a new glyph on top of the base one */
  FT_BASE_DEF( void )
  FT_GlyphLoader_Prepare( FT_GlyphLoader   loader )
  {
    FT_GlyphLoad  current = &loader->current;


    current->outline.n_points   = 0;
    current->outline.n_contours = 0;
    current->num_subglyphs      = 0;

    FT_GlyphLoader_Adjust_Points   ( loader );
    FT_GlyphLoader_Adjust_Subglyphs( loader );
  }


  /* add current glyph to the base image - and prepare for another */
  FT_BASE_DEF( void )
  FT_GlyphLoader_Add( FT_GlyphLoader   loader )
  {
    FT_GlyphLoad   base    = &loader->base;
    FT_GlyphLoad   current = &loader->current;

    FT_UInt        n_curr_contours = current->outline.n_contours;
    FT_UInt        n_base_points   = base->outline.n_points;
    FT_UInt        n;


    base->outline.n_points =
      (short)( base->outline.n_points + current->outline.n_points );
    base->outline.n_contours =
      (short)( base->outline.n_contours + current->outline.n_contours );

    base->num_subglyphs += current->num_subglyphs;

    /* adjust contours count in newest outline */
    for ( n = 0; n < n_curr_contours; n++ )
      current->outline.contours[n] =
        (short)( current->outline.contours[n] + n_base_points );

    /* prepare for another new glyph image */
    FT_GlyphLoader_Prepare( loader );
  }


  FT_BASE_DEF( FT_Error )
  FT_GlyphLoader_Copy_Points( FT_GlyphLoader   target,
                              FT_GlyphLoader   source )
  {
    FT_Error  error;
    FT_UInt   num_points   = source->base.outline.n_points;
    FT_UInt   num_contours = source->base.outline.n_contours;


    error = FT_GlyphLoader_Check_Points( target, num_points, num_contours );
    if ( !error )
    {
      FT_Outline*  out = &target->base.outline;
      FT_Outline*  in  = &source->base.outline;


      MEM_Copy( out->points, in->points,
                num_points * sizeof ( FT_Vector ) );
      MEM_Copy( out->tags, in->tags,
                num_points * sizeof ( char ) );
      MEM_Copy( out->contours, in->contours,
                num_contours * sizeof ( short ) );

      /* do we need to copy the extra points? */
      if ( target->use_extra && source->use_extra )
        MEM_Copy( target->base.extra_points, source->base.extra_points,
                  num_points * sizeof ( FT_Vector ) );

      out->n_points   = (short)num_points;
      out->n_contours = (short)num_contours;

      FT_GlyphLoader_Adjust_Points( target );
    }

    return error;
  }


/* END */