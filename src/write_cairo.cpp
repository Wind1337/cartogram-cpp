#include <cairo/cairo.h>
#include <cairo/cairo-ps.h>
#include <cairo/cairo-pdf.h>
#include "cartogram_info.h"
#include "constants.h"
#include "inset_state.h"

void write_ps_header(const std::string filename,
                      cairo_surface_t *surface) 
{
  const std::string title = "%%Title: " + filename;
  cairo_ps_surface_dsc_comment(surface, title.c_str());
  cairo_ps_surface_dsc_comment(surface,
                               "%%Creator: Michael T. Gastner et al.");
  cairo_ps_surface_dsc_comment(surface,
                               "%%For: Humanity");
  cairo_ps_surface_dsc_comment(surface,
                               "%%Copyright: License CC BY");
  cairo_ps_surface_dsc_comment(surface,
                               "%%Magnification: 1.0000");                                            
}

// TODO: IS THERE A CGAL WAY OF DETERMINING WHETHER THE LABEL'S BOUNDING
//       BOX IS COMPLETELY CONTAINED IN THE POLYGON?

// TODO: SHOULD THE CRITERION FOR PRINTING A LABEL BE THAT IT FITS INSIDE THE
//       POLYGON WITH HOLES? THAT CRITERION WOULD BE MORE RESTRICTIVE THAN
//       FITTING INSIDE THE EXTERIOR RING.
bool all_points_inside_exterior_ring(const std::vector<Point> pts,
                                     const Polygon_with_holes pwh)
{
  for (const auto &pt : pts) {
    if (pwh.outer_boundary().has_on_unbounded_side(pt)) {
      return false;
    }
  }
  return true;
}

double font_size(cairo_t *cr,
                 const char *label,
                 const Point label_pt,
                 const GeoDiv gd)
{
  cairo_text_extents_t extents;
  for (double fsize = max_font_size; fsize >= min_font_size; fsize -= 0.5) {
    cairo_set_font_size(cr, fsize);
    cairo_text_extents(cr, label, &extents);
    const auto largest_pwh = gd.largest_polygon_with_holes();

    // Bounding box of the label
    const CGAL::Bbox_2 bb(label_pt.x() - 0.5*extents.width,
                          label_pt.y() - 0.5*extents.height,
                          label_pt.x() + 0.5*extents.width,
                          label_pt.y() + 0.5*extents.height);

    // Vector of bounding-box edge points
    std::vector<Point> bb_edge_points;
    for (unsigned int i = 0; i <= 1; ++i) {
      for (unsigned int j = 0; j <= 5; ++j) {
        bb_edge_points.push_back(
          Point((j*bb.xmin() + (5 - j) * bb.xmax()) / 5,
                (i*bb.ymin() + (1 - i) * bb.ymax()))
          );
      }
    }
    if (all_points_inside_exterior_ring(bb_edge_points, largest_pwh)) {
      return fsize;
    }
  }
  return 0.0;
}

void write_labels_to_cairo_surface(cairo_t *cr,
                                   const InsetState *inset_state)
{
  const auto ly = inset_state->ly();
  for (const auto &gd : inset_state->geo_divs()) {
    const auto label = inset_state->label_at(gd.id());
    const auto label_char = label.c_str();
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_select_font_face(cr,
                           "sans-serif",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    const auto label_pt = gd.point_on_surface_of_geodiv();

    // Get size of label
    const auto fsize = font_size(cr, label_char, label_pt, gd);
    cairo_text_extents_t extents;
    
    // Draw label only if appropriate size is found
    if (fsize > 0.0) {
      cairo_set_font_size(cr, fsize);
      cairo_text_extents(cr, label_char, &extents);
      const double x =
        label_pt.x() - (extents.width / 2 + extents.x_bearing);
      const double y =
        ly - label_pt.y() - (extents.height / 2 + extents.y_bearing);
      cairo_move_to(cr, x, y);
      cairo_show_text(cr, label_char);
    }
  }
}

// Returns edge points of a graticule cell and treats 
// them as a polygon.
Polygon graticule_cell_edge_points(unsigned int x,
                                unsigned int y,
                                InsetState *inset_state,
                                unsigned int cell_width = graticule_width)
{
  Polygon cell_edge_points;
  const boost::multi_array<XYPoint, 2> &cum_proj =
    *inset_state->ref_to_cum_proj();
    
  // Horizontal lower edge points
  for (unsigned int i = x; i < x + cell_width; ++i) {
    auto x_coor_trans = cum_proj[i][y].x;
    auto y_coor_trans = cum_proj[i][y].y;
    cell_edge_points.push_back(Point(x_coor_trans, y_coor_trans));
  }
  
  // Vertical right edge points
  for (unsigned int i = y; i < y + cell_width; ++i) {
    auto x_coor_trans = cum_proj[x + cell_width][i].x;
    auto y_coor_trans = cum_proj[x + cell_width][i].y;
    cell_edge_points.push_back(Point(x_coor_trans, y_coor_trans));
  }

  // Horizontal upper edge points
  for (unsigned int i = x + cell_width; i > x ; --i) {
    auto x_coor_trans = cum_proj[i][y + cell_width].x;
    auto y_coor_trans = cum_proj[i][y + cell_width].y;
    cell_edge_points.push_back(Point(x_coor_trans, y_coor_trans));
  }

  // Vertical left edge points
  for (unsigned int i = y + cell_width; i > y ; --i) {
    auto x_coor_trans = cum_proj[x][i].x;
    auto y_coor_trans = cum_proj[x][i].y;
    cell_edge_points.push_back(Point(x_coor_trans, y_coor_trans));
  }

  // Complete the polygon by making first and last point same
  cell_edge_points.push_back(cell_edge_points[0]);
  return cell_edge_points;    
}

// Returns graticule cell area based on edge points
double graticule_cell_area(unsigned int x,
                          unsigned int y,
                          InsetState *inset_state,
                          unsigned int cell_width)
{
  
  // Taking absolule to ensure we get the area irrespective of direction
  return abs(graticule_cell_edge_points(x, y, inset_state, cell_width).area());
}

// Returns the largest and smallest graticule cell area to be used for
// graticule heatmap generation 
std::pair<double,double> max_and_min_graticule_cell_area(InsetState *inset_state,
                                                          unsigned int cell_width)
{
  auto lx = inset_state->lx();
  auto ly = inset_state->ly();
  
  // Initialize max and min area
  double max_area = -dbl_inf;
  double min_area = dbl_inf;
  
  // Iterate over graticule cells
  for (unsigned int i = 0; i < lx - cell_width; i += cell_width) {
    for(unsigned int j = 0; j < ly - cell_width; j += cell_width) {
      const auto area = graticule_cell_area(i, j, inset_state, cell_width);
      if (area > max_area) {
        max_area = area;
      }
      if (area < min_area) {
        min_area = area;
      }
    }
  }
  return std::make_pair(max_area, min_area);
}

// Sets the color of a graticule cell based on its area
void graticule_cell_color(double area,
                          double max_area,
                          double min_area,
                          double *r,
                          double *g,
                          double *b) 
{
  
  // For input map when all cells are of same area
  if (max_area - min_area < 1e-6) {
    *r = 1.0 ;
    *g = 1.0 ;
    *b = 1.0 ;
  } else {
    
    // Normalize area to [0,1] and make it logarithmic
    double ratio = (log(area) - log(min_area))/ (log(max_area) - log(min_area));
    *r = 1.0 - ratio;
    *g = 1.0 - ratio;
    *b = 1.0;
  }
}

// Writes graticule cells and colors them if required
void write_graticules_to_cairo_surface(cairo_t *cr,
                                      bool plot_graticule_heatmap,
                                      InsetState *inset_state)
{
  const auto lx = inset_state->lx();
  const auto ly = inset_state->ly();

  // Set line width of graticule lines
  cairo_set_line_width(cr, 5e-4 * std::min(lx, ly));
  cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);

  // Iterate over graticule cells
  for (unsigned int i = 0; i < lx - graticule_width; i += graticule_width) {
    for(unsigned int j = 0; j < ly - graticule_width; j += graticule_width) {
      
      // Draw graticule cell by connecting edge points
      const auto cell_edge_points = graticule_cell_edge_points(i, j, inset_state);                                           
      cairo_move_to(cr, cell_edge_points[0].x(), ly - cell_edge_points[0].y());
      for (unsigned int k = 1; k < cell_edge_points.size(); ++k) {
        cairo_line_to(cr, cell_edge_points[k].x(), ly - cell_edge_points[k].y());
      }
      cairo_stroke(cr);
    }
  }
}

void write_graticule_colors_to_cairo_surface(cairo_t *cr,
                                             InsetState *inset_state)
{
  const auto lx = inset_state->lx();
  const auto ly = inset_state->ly();
  unsigned int cell_width = 1;
  
  // Get max and min area of graticule cells
  const auto max_and_min_area = max_and_min_graticule_cell_area(inset_state, cell_width);
  const auto max_area = max_and_min_area.first;
  const auto min_area = max_and_min_area.second;
  
  // Set line width of graticule lines
  cairo_set_line_width(cr, 5e-6 * std::min(lx, ly));
  
  // Iterate over graticule cells
  for (unsigned int i = 0; i < lx - cell_width; i += cell_width) {
    for(unsigned int j = 0; j < ly - cell_width; j += cell_width) {
      double r, g, b;
      graticule_cell_color(graticule_cell_area(i, j, inset_state, cell_width),
                             max_area,
                             min_area,
                             &r,
                             &g,
                             &b);
      
      // Set color of the border of the graticule polygon           
      cairo_set_source_rgb(cr, r, g, b); 
                 
      // Draw graticule cell by connecting edge points
      const auto cell_edge_points = graticule_cell_edge_points(i, j, inset_state, cell_width);                                        
      cairo_move_to(cr, cell_edge_points[0].x(), ly - cell_edge_points[0].y());
      for (unsigned int k = 1; k < cell_edge_points.size(); ++k) {
        cairo_line_to(cr, cell_edge_points[k].x(), ly - cell_edge_points[k].y());
      }
      
      // Fill the graticule polygon with color
      cairo_fill_preserve(cr);
      cairo_stroke(cr);
    }
  }
}

void write_polygons_to_cairo_surface(cairo_t *cr,
                                    const bool fill_polygons,
                                    const bool colors,
                                    const bool draw_equal_area_map,
                                    InsetState *inset_state)
{
  const auto lx = inset_state->lx();
  const auto ly = inset_state->ly();
  cairo_set_line_width(cr, 1e-3 * std::min(lx, ly));
  std::vector<GeoDiv> geo_divs;
  
  // Draw cartogram polygons or equal area map polygons
  if (draw_equal_area_map) {
    geo_divs = inset_state->geo_divs_original();
  } else {
    geo_divs = inset_state->geo_divs();
  }
  
  // Draw the shapes
  for (const auto &gd : geo_divs) {
    for (const auto &pwh : gd.polygons_with_holes()) {
      const auto ext_ring = pwh.outer_boundary();
      cairo_move_to(cr, ext_ring[0].x(), ly - ext_ring[0].y());
      
      // Plot each point in exterior ring
      for (unsigned int i = 1; i < ext_ring.size(); ++i) {
        cairo_line_to(cr, ext_ring[i].x(), ly - ext_ring[i].y());
      }
      
      // Close the exterior ring
      cairo_close_path(cr);
      
      // Draw holes
      for (auto h = pwh.holes_begin(); h != pwh.holes_end(); ++h) {
        cairo_move_to(cr, (*h)[0].x(), ly - (*h)[0].y());
        const auto hsize = (*h).size();
        for (unsigned int i = 1; i <= hsize; ++i) {
          cairo_line_to(cr, (*h)[i % hsize].x(), ly - (*h)[i % hsize].y());
        }
      }
      if (colors || fill_polygons) {
        if (inset_state->is_input_target_area_missing(gd.id())) {

          // Fill path with dark gray
          cairo_set_source_rgb(cr, 0.9375, 0.9375, 0.9375);
        } else if (colors) {

          // Get color
          const auto col = inset_state->color_at(gd.id());

          // Fill path
          cairo_set_source_rgb(cr,
                               col.r / 255.0,
                               col.g / 255.0,
                               col.b / 255.0);
        } else if (fill_polygons) {

          // Fill path with default color
          cairo_set_source_rgb(cr, 0.96, 0.92, 0.70);
        }
        cairo_fill_preserve(cr);
      }

      cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
      cairo_stroke(cr);
    }
  }
}

// Outputs a PS file with polygons, labels, and graticules (if required)
void write_map_to_ps(const std::string fname,
                    const bool fill_polygons,
                    const bool plot_graticule,
                    InsetState *inset_state)
{
  const auto filename = fname.c_str();
  const auto lx = inset_state->lx();
  const auto ly = inset_state->ly();
  
  // Check whether the map has all GeoDivs colored
  const bool colors =
    (inset_state->colors_size() == inset_state->n_geo_divs());
  cairo_surface_t *surface;
  cairo_t *cr;
  surface = cairo_ps_surface_create(filename, lx, ly);
  cr = cairo_create(surface);
  
  // Write header
  write_ps_header(fname, surface);
  
  // Draw polygons with color
  write_polygons_to_cairo_surface(cr,
                                 fill_polygons,
                                 colors,
                                 false,
                                 inset_state);
                                 
  // Place labels
  write_labels_to_cairo_surface(cr, inset_state);
  
  // Draw graticule without color
  if (plot_graticule) {
    write_graticules_to_cairo_surface(cr, false, inset_state);
  }
  cairo_show_page(cr);
  cairo_surface_destroy(surface);
  cairo_destroy(cr);
}

// Outputs a PS file of graticule heatmap
void write_graticule_heatmap_to_ps(const std::string ps_name,
                                  const bool draw_equal_area_map,
                                  InsetState *inset_state)
{
  auto filename = ps_name.c_str();
  const auto lx = inset_state->lx();
  const auto ly = inset_state->ly();
  cairo_surface_t *surface;
  cairo_t *cr;
  surface = cairo_ps_surface_create(filename, lx, ly);
  cr = cairo_create(surface);
  
  // Write header
  write_ps_header(ps_name, surface);
  
  // Draw colors
  write_graticule_colors_to_cairo_surface(cr, inset_state);
  
  // Draw graticules
  if (not draw_equal_area_map) {
    write_graticules_to_cairo_surface(cr, true, inset_state);
  }
  
  // Draw polygons without color
  write_polygons_to_cairo_surface(cr,
                              false,
                              false,
                              draw_equal_area_map,
                              inset_state);                        
  cairo_show_page(cr);
  cairo_surface_destroy(surface);
  cairo_destroy(cr);
}

// Functions to show a scalar field called "density" as a heat map
double interpolate_for_heatmap(const double x,
                               const double xmin,
                               const double xmax,
                               const double ymin,
                               const double ymax)
{
  return ((x - xmin) * ymax + (xmax - x) * ymin) / (xmax - xmin);
}

void heatmap_color(const double dens,
                   const double dens_min,
                   const double dens_mean,
                   const double dens_max,
                   double *r,
                   double *g,
                   double *b)
{
  // Assign possible categories for red, green, blue
  const double red[] = {
    0.33, 0.55, 0.75, 0.87, 0.96, 0.99, 0.78, 0.50, 0.21, 0.00, 0.00
  };
  const double green[] = {
    0.19, 0.32, 0.51, 0.76, 0.91, 0.96, 0.92, 0.80, 0.59, 0.40, 0.24
  };
  const double blue[] = {
    0.02, 0.04, 0.18, 0.49, 0.76, 0.89, 0.90, 0.76, 0.56, 0.37, 0.19
  };
  double xmin, xmax;
  int color_category;

  // Choose color category
  if (dens > dens_max) {
    *r = red[0];
    *g = green[0];
    *b = blue[0];
    return;
  } else if (dens > dens_mean) {
    color_category = 5 * (dens_max - dens) / (dens_max - dens_mean);
    xmax = dens_max - 0.2 * color_category * (dens_max - dens_mean);
    xmin = xmax - 0.2 * (dens_max - dens_mean);

    // Assign color category 0 if dens_max and dens are very close
    color_category = std::max(color_category, 0);
  } else if (dens > dens_min) {
    color_category = 5 * (dens_mean - dens) / (dens_mean - dens_min) + 5;
    xmax = dens_mean - 0.2 * (color_category - 5) * (dens_mean - dens_min);
    xmin = xmax - 0.2 * (dens_mean - dens_min);

    // Assign color category 9 if dens_min and dens are very close
    color_category = std::min(color_category, 9);
  } else {
    *r = red[10];
    *g = green[10];
    *b = blue[10];
    return;
  }
  *r = interpolate_for_heatmap(dens,
                               xmin,
                               xmax,
                               red[color_category + 1],
                               red[color_category]);
  *g = interpolate_for_heatmap(dens,
                               xmin,
                               xmax,
                               green[color_category + 1],
                               green[color_category]);
  *b = interpolate_for_heatmap(dens,
                               xmin,
                               xmax,
                               blue[color_category + 1],
                               blue[color_category]);
}

void write_density_to_ps(const std::string eps_name,
                          const double *density,
                          InsetState *inset_state)
{
  auto filename = eps_name.c_str();
  const auto lx = inset_state->lx();
  const auto ly = inset_state->ly();
  cairo_surface_t *surface;
  cairo_t *cr;
  surface = cairo_ps_surface_create(filename, lx, ly);
  cr = cairo_create(surface);
  
  // Write header
  write_ps_header(eps_name, surface);
  
  cairo_set_line_width(cr, 0);
  // Determine range of densities
  double dens_min = dbl_inf;
  double dens_mean = 0.0;
  double dens_max = -dbl_inf;
  const unsigned int n_grid_cells = inset_state->lx() * inset_state->ly();
  for (unsigned int k = 0; k < n_grid_cells; ++k) {
    dens_min = std::min(density[k], dens_min);
    dens_mean += density[k];
    dens_max = std::max(density[k], dens_max);
  }
  dens_mean /= n_grid_cells;
  for (unsigned int i = 0; i < inset_state->lx(); ++i) {
    for (unsigned int j = 0; j < inset_state->ly(); ++j) {
      double r, g, b;
      heatmap_color(density[i*inset_state->ly() + j],
                    dens_min,
                    dens_mean,
                    dens_max,
                    &r, &g, &b);
      // Get four points of the square
      double x_min = i - 0.5*sq_overlap;
      double y_min = j - 0.5*sq_overlap;
      double x_max = x_min + 1.2;
      double y_max = y_min + 1.2;
      
      cairo_move_to(cr, x_min, ly - y_min);
      cairo_line_to(cr, x_max, ly - y_min);
      cairo_line_to(cr, x_max, ly - y_max);
      cairo_line_to(cr, x_min, ly - y_max);
      
      cairo_set_source_rgb(cr, r, g, b);
      cairo_fill(cr);
      cairo_set_source_rgb(cr, 0, 0, 0);
      cairo_stroke(cr);
      
    }
  }
  write_polygons_to_cairo_surface(cr,
                                false,
                                false,
                                false,
                                inset_state);
  cairo_show_page(cr);
  cairo_surface_destroy(surface);
  cairo_destroy(cr);
}

void InsetState::write_intersections_to_ps(unsigned int res)
{
  std::string eps_name =
    inset_name() +
    "_cairo_intersections_" +
    std::to_string(n_finished_integrations()) +
    ".ps";

  // Calculating intersections
  std::vector<Segment> intersections = intersecting_segments(res);

  auto filename = eps_name.c_str();
  cairo_surface_t *surface;
  cairo_t *cr;
  surface = cairo_ps_surface_create(filename, lx_, ly_);
  cr = cairo_create(surface);
  
  // Write header
  write_ps_header(inset_name(), surface);
                               
  write_polygons_to_cairo_surface(cr,
                              false,
                              false,
                              false,
                              this);
                              
  cairo_set_line_width(cr, 0.0001 * std::min(lx_, ly_));

  for (auto seg : intersections) {

    // Move to starting coordinates
    cairo_move_to(cr, seg[0][0], ly_ - seg[0][1]);

    // Draw line
    cairo_line_to(cr, seg[1][0], ly_ - seg[1][1]);

    // line with red and stroke
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_stroke(cr);
  }
  cairo_show_page(cr);
  cairo_surface_destroy(surface);
  cairo_destroy(cr);
}

