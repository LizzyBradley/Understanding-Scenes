// Source file for the scene viewer program



// Include files 

#include "R3Graphics/R3Graphics.h"
#include "fglut/fglut.h"
#include "p5d.h"

#include "csv.h"
#include <string>
#include  <map>

// Program variables

static const char *input_project_name = NULL;
//static const char *input_data_directory = "../..";
static const char *input_data_directory = "data/";
static const char *output_grid_directory = NULL;
static int print_verbose = 0;
static int print_debug = 0;

static std::map<std::string, std::string> id_to_category;
static int pixels_to_meters = 50;
static int meters_of_context = 4;
static int resolution = pixels_to_meters * (2 * meters_of_context);

static P5DProject *
ReadProject(const char *project_name)
{
  // Start statistics
  RNTime start_time;
  start_time.Read();

  // Allocate scene
  P5DProject *project = new P5DProject();
  if (!project) {
    fprintf(stderr, "Unable to allocate project for %s\n", project_name);
    return NULL;
  }

  // Set project name
  project->name = strdup(project_name);

  // Read project from file
  char filename[4096];
  sprintf(filename, "%s/projects/%s/project.json", input_data_directory, project_name);
  if (!project->ReadFile(filename)) {
    delete project;
    return NULL;
  }

  // Print statistics
  if (print_verbose) {
    printf("Read project from %s ...\n", filename);
    printf("  Time = %.2f seconds\n", start_time.Elapsed());
    fflush(stdout);
  }

  // Temporary
  if (print_debug) project->Print();
  
  // Return project
  return project;
}



static int 
WriteGrid(R2Grid *grid, const char *filename)
{
  // Start statistics
  RNTime start_time;
  start_time.Read();

  // Write grid
  int status = grid->Write(filename);

  // Print statistics
  if (print_verbose) {
    printf("Wrote grid to %s\n", filename);
    printf("  Time = %.2f seconds\n", start_time.Elapsed());
    printf("  Resolution = %d %d\n", grid->XResolution(), grid->YResolution());
    printf("  Spacing = %g\n", grid->GridToWorldScaleFactor());
    printf("  Cardinality = %d\n", grid->Cardinality());
    RNInterval grid_range = grid->Range();
    printf("  Minimum = %g\n", grid_range.Min());
    printf("  Maximum = %g\n", grid_range.Max());
    printf("  L1Norm = %g\n", grid->L1Norm());
    printf("  L2Norm = %g\n", grid->L2Norm());
    fflush(stdout);
  }

  // Return status
  return status;
}



static R3Scene *
CreateScene(P5DProject *project)
{
  // Start statistics
  RNTime start_time;
  start_time.Read();

  // Allocate scene
  R3Scene *scene = new R3Scene();
  if (!scene) {
    fprintf(stderr, "Unable to allocate scene\n");
    return NULL;
  }

  // Get node for project
  R3SceneNode *root_node = scene->Root();
  root_node->SetName("Project");

  // Create nodes for floors
  RNScalar floor_z = 0;
  for (int i = 0; i < project->NFloors(); i++) {
    P5DFloor *floor = project->Floor(i);

    // Create floor node
    char floor_name[1024];
    sprintf(floor_name, "Floor_%d", i);
    R3SceneNode *floor_node = new R3SceneNode(scene);
    floor_node->SetName(floor_name);
    floor_node->SetData(floor);
    floor->data = floor_node;
    root_node->InsertChild(floor_node);

    // Compute floor transformation
    R3Affine floor_transformation(R3identity_affine);
    floor_transformation.Translate(R3Vector(0, 0, floor_z));
    floor_z += floor->h;

    // Create nodes for rooms
    for (int j = 0; j < floor->NRooms(); j++) {
      P5DRoom *room = floor->Room(j);

      // Create room node
      char room_name[1024];
      sprintf(room_name, "Room_%d", j);
      R3SceneNode *room_node = new R3SceneNode(scene);
      room_node->SetName(room_name);
      room_node->SetData(room);
      room->data = room_node;
      floor_node->InsertChild(room_node);

      // Check if enclosed room
      if (!strcmp(room->className, "Room")) {
        // Read walls
        char rm_name[4096], node_name[4096];
        sprintf(rm_name, "%s/roomfiles/%s/fr_%drm_%d.obj", input_data_directory, project->name, i+1, room->idx_index+1); //fr_1rm_1.obj
        R3SceneNode *wall_node = new R3SceneNode(scene);
        sprintf(node_name, "Wall_%d", j);
        wall_node->SetName(node_name);

        if (!ReadObj(scene, wall_node, rm_name)) return 0;
        room_node->InsertChild(wall_node);

        // Read floor
        sprintf(rm_name, "%s/roomfiles/%s/fr_%drm_%df.obj", input_data_directory, project->name, i+1, room->idx_index+1); //fr_1rm_1.obj
        R3SceneNode *rmfloor_node = new R3SceneNode(scene);
        sprintf(node_name, "Floor_%d", j);
        rmfloor_node->SetName(node_name);
        if (!ReadObj(scene, rmfloor_node, rm_name)) return 0;
        room_node->InsertChild(rmfloor_node);
        
        // Read ceiling
        // sprintf(rm_name, "%s/roomfiles/%s/fr_%drm_%dc.obj", input_data_directory, project->name, i+1, room->idx_index+1); //fr_1rm_1.obj
        // R3SceneNode *rmceil_node = new R3SceneNode(scene);
        // sprintf(node_name, "Ceiling_%d", j);
        // rmceil_node->SetName(node_name);
        // if (!ReadObj(scene, rmceil_node, rm_name)) return 0;
        // room_node->InsertChild(rmceil_node);
      }
    }

    // Create nodes for objects
    for (int j = 0; j < floor->NObjects(); j++) {
      P5DObject *object = floor->Object(j);
      if (!object->id || !(*(object->id))) continue;

      // Create object node
      char object_name[1024];
      sprintf(object_name, "Object_%d_%s", j, object->id);
      R3SceneNode *object_node = new R3SceneNode(scene);
      object_node->SetName(object_name);
      object_node->SetData(object);
      object->data = object_node;

      // Insert into room or floor parent node
      R3SceneNode *parent_node = (object->room) ? floor_node->Child(object->room->floor_index) : floor_node;
      parent_node->InsertChild(object_node);

      // Compute object transformation
      R3Affine object_transformation(R3identity_affine);
      object_transformation.Translate(R3Vector(object->x, object->y, object->z));
      object_transformation.ZRotate(object->a);
      object_transformation.Scale(R3Vector(object->sX, object->sY, object->sZ));
      if (!strcmp(object->className, "Door")) object_transformation.ZRotate(RN_PI_OVER_TWO);
      else if (!strcmp(object->className, "Window")) object_transformation.ZRotate(RN_PI_OVER_TWO);
      if (object->fX) object_transformation.XMirror();
      if (object->fY) object_transformation.YMirror();

      // Read obj file
      char obj_name[4096];
      sprintf(obj_name, "%s/objects/%s/%s.obj", input_data_directory, object->id, object->id);
      if (!ReadObj(scene, object_node, obj_name)) return 0;

      // Apply transformation
      R3Affine transformation = R3identity_affine;
      transformation.Transform(floor_transformation);
      transformation.Transform(object_transformation);
      for (int k = 0; k < object_node->NElements(); k++) {
        R3SceneElement *element = object_node->Element(k);
        for (int s = 0; s < element->NShapes(); s++) {
          R3Shape *shape = element->Shape(s);
          shape->Transform(transformation);
        }
      }
    }
  }

  // Print statistics
  if (print_verbose) {
    printf("Created scene ...\n");
    printf("  Time = %.2f seconds\n", start_time.Elapsed());
    printf("  # Nodes = %d\n", scene->NNodes());
    fflush(stdout);
  }

  // Return scene
  return scene;
}

std::string GetObjectCategory(R3SceneNode* obj) 
{
    // Parse object name for id
    std::string name (obj->Name());

    size_t pos = name.find_last_of("_") + 1;
    if (name[pos - 2] == '_') { // for the s__* series
        pos = name.find_last_of("_", pos - 3) + 1;
    }

    if (pos == std::string::npos) {
        fprintf(stdout, "FAILURE. Malformed object name: %s\n", name.c_str());
        exit(-1);
    }
    
    std::string id = name.substr(pos);

    // Lookup id in id_to_category_map
    auto cat_iter = id_to_category.find(id);
    if (cat_iter == id_to_category.end()) {
        fprintf(stdout, "FAILURE. Unexpected object Id: %s\n", id.c_str());
        exit(-1);
    }

    std::string cat = cat_iter->second;
    
    if (print_verbose) {
        fprintf(stdout, "Id: %s Category: %s\n", id.c_str(), cat.c_str());
    }

    return cat;
}

void LoadIdToCategoryMap(std::string csv_filename="object_names.csv") 
{
    io::CSVReader<2, io::trim_chars<' '>, io::double_quote_escape<',','\"'> > in(csv_filename.c_str());
    in.read_header(io::ignore_extra_column, "id", "category");
    std::string id; std::string category;
    while(in.read_row(id, category)) {
        id_to_category[id] = category;
    }
}

R2Box Get2DBoundingBox(R3SceneNode* node) 
{
    R3Box box = node->BBox();
    return R2Box(box.XMin(), box.YMin(), box.XMax(), box.YMax());
}

std::vector<R2Point> GetCorners(R2Box box) 
{
    return std::vector<R2Point> { box.Min(), box.Max()};
}

static R2Point 
FixPoint(R2Grid* grid, R2Point v)
{
    R2Point grid_v = grid->GridPosition(v);
    RNScalar x = grid_v.X();
    RNScalar y = grid_v.Y();

    if (x >= resolution)
        x = resolution - 1;
    else if (x < 0)
        x = 0;
   
    if (y >= resolution - 1)
        y = resolution - 1;
    else if (y < 0)
        y = 0;

    //if (x != grid_v.X() || y != grid_v.Y())
    //    fprintf(stderr, "Fixed a point\n");

    grid_v = grid->WorldPosition(x, y);

    return grid_v;
}

static bool
VertexOutOfGrid(R2Grid* grid, R2Point v)
{
    R2Point grid_v = grid->GridPosition(v);

    if ((grid_v.X() >= resolution || grid_v.X() < 0) &&
        (grid_v.Y() >= resolution || grid_v.Y() < 0))
        return true;

    return false;
}

static bool
AllVerticesOutOfGrid(R2Grid* grid, R2Point v0, R2Point v1, R2Point v2)
{
    return VertexOutOfGrid(grid, v0) || VertexOutOfGrid(grid, v1) || VertexOutOfGrid(grid, v1);
}

static std::vector<R2Point>
SanitizePoints(R2Grid* grid, R2Point v0, R2Point v1, R2Point v2)
{ 
    R2Point fix_v0 = FixPoint(grid, v0);
    R2Point fix_v1 = FixPoint(grid, v1);
    R2Point fix_v2 = FixPoint(grid, v2);

    std::vector<R2Point> v = { fix_v0, fix_v1, fix_v2 };
    return v;

}

class Stats
{
    public:
        float xMax;
        float xMin = FLT_MAX;
        float yMax;
        float yMin = FLT_MAX;
};

static Stats*
UpdateStats(Stats* stats, R2Point v) 
{
    if (v.X() > stats->xMax)
        stats->xMax = v.X();
    if (v.X() < stats->xMin)
        stats->xMin = v.X();
    if (v.Y() < stats->yMin)
        stats->yMin = v.Y();
    if (v.Y() > stats->yMax)
        stats->yMax = v.Y();

    return stats;
}

static float threshold = 0.2;

static Stats*
UpdateStats(Stats* stats, R2Point v0, R2Point v1, R2Point v2) {
    stats = UpdateStats(stats, v0);
    stats = UpdateStats(stats, v1);
    stats = UpdateStats(stats, v2);

    return stats;
}

static bool
IsCloseEnough(float v1, float v2) 
{
    if (fabs(v1 - v2) <= threshold)
        return true;
    
    return false;
}

static bool 
IsSameWall(Stats* stats, Stats* old_stats) {
    bool match_xMax = IsCloseEnough(stats->xMax, old_stats->xMax);
    bool match_xMin = IsCloseEnough(stats->xMin, old_stats->xMin);
    bool match_yMax = IsCloseEnough(stats->yMax, old_stats->yMax);
    bool match_yMin = IsCloseEnough(stats->yMin, old_stats->yMin);
    
    if (match_xMax && match_xMin)
        fprintf(stderr, "\nMatched x's : (%f, %f) (%f, %f)\n", stats->xMax, old_stats->xMax,
                stats->xMin, old_stats->xMin);
    if (match_xMax && match_xMin)
        fprintf(stderr, "\nMatched y's : (%f, %f) (%f, %f)\n", stats->yMax, old_stats->yMax,
                stats->yMin, old_stats->yMin);
    //if ((match_xMax && match_xMin) && NotTooFar( // Need to set a limit so parallel walls do not enter in

    return (match_xMax && match_xMin) || (match_yMax && match_yMin);
}

class Wall
{
    public:
        Stats* stats;
        std::vector<R3Triangle*> triangles;
};


static R2Grid*
DrawWall(R3SceneNode* wall, R2Grid *grid, R2Vector translation, RNAngle theta, R2Vector *dist_ptr = NULL)
{
    R2Grid* temp_grid = new R2Grid(resolution, resolution);

    fprintf(stderr, "Drawing  Wall %s ...\n", wall->Name());

    R2Vector centroid = R2Vector(wall->Centroid().X(), wall->Centroid().Y());

    R2Affine world_to_grid_transformation = R2identity_affine;
    if (dist_ptr != NULL) {
        world_to_grid_transformation.Translate(*dist_ptr * pixels_to_meters);
        world_to_grid_transformation.Translate(-1.0 * *dist_ptr);
    }
    world_to_grid_transformation.Translate(translation);
    world_to_grid_transformation.Translate(centroid);
    world_to_grid_transformation.Scale(pixels_to_meters);
    world_to_grid_transformation.Rotate(-1.0 * theta);
    world_to_grid_transformation.Translate(-1.0 * centroid);

    temp_grid->SetWorldToGridTransformation(world_to_grid_transformation);
    fprintf(stderr, "Number of Elements: %d", wall->NElements());
    
    //std::vector<std::vector<R3Triangle*>> walls;
    std::vector<Wall*> walls;

    // For all R3SceneElements in the R3SceneNode
    for (int k = 0; k < wall->NElements(); k++) {
        R3SceneElement* el = wall->Element(k);

        // Start by assuming new wall
        Wall* new_wall = new Wall();
        new_wall->stats = new Stats();
        new_wall->triangles = std::vector<R3Triangle*>();
        
        // For all R3Shapes in the R3SceneElements    
        for (int l = 0; l < el->NShapes(); l++) {

            //fprintf(stderr, "\t Number of Shapes: %d\n", el->NShapes());
            R3Shape* shape = el->Shape(l);
            R3TriangleArray* arr = (R3TriangleArray*) shape;
           
            // For all R3Triangles in the R3TriangleArray
            for (int t = 0; t < arr->NTriangles(); t++) {
                //fprintf(stderr, "Starting... ");
                R3Triangle *triangle = arr->Triangle(t);
                

                // Create new points
                R2Point v0 = R2Point(triangle->V0()->Position().X(), triangle->V0()->Position().Y());
                R2Point v1 = R2Point(triangle->V1()->Position().X(), triangle->V1()->Position().Y());
                R2Point v2 = R2Point(triangle->V2()->Position().X(), triangle->V2()->Position().Y());
                UpdateStats(new_wall->stats, v0, v1, v2);

                //fprintf(stderr, "\t\t(%f, %f) (%f, %f) (%f, %f)\n", v0.X(), v0.Y(), v1.X(), v1.Y(), v2.X(), v2.Y());

                if (AllVerticesOutOfGrid(temp_grid, v0, v1, v2)) continue;
                std::vector<R2Point> v = SanitizePoints(temp_grid, v0, v1, v2);

                if (VertexOutOfGrid(temp_grid, v[0]) || VertexOutOfGrid(temp_grid, v[1]) || VertexOutOfGrid(temp_grid, v[2]))
                    continue;
               
                new_wall->triangles.push_back(triangle); 
                temp_grid->RasterizeWorldTriangle(v[0], v[1], v[2], 1);

            }
        }


        bool isNewWall = true;
        // Is this a new wall?
        for (Wall* old_wall : walls) {
            
            Stats* old_stats = old_wall->stats;
            std::vector<R3Triangle*> old_triangles = old_wall->triangles;

            if (IsSameWall(new_wall->stats, old_stats)) {
                fprintf(stderr, "\nFound same wall! \t ");
                old_triangles.reserve(old_triangles.size() + new_wall->triangles.size());
                old_triangles.insert(old_triangles.end(), new_wall->triangles.begin(), new_wall->triangles.end());
                //fprintf(stderr, "new_wall->triangles.size(): %d old_triangles.size(): %d\n", new_wall->triangles.size(), old_triangles.size());
                old_wall->triangles = old_triangles;
                isNewWall = false;
                break;
            }
        }

        if (isNewWall) {
            fprintf(stderr, "\t\tAdded new wall\n");
            walls.push_back(new_wall);
        }

        char output_filename[1024];
        sprintf(output_filename, "Segment_%d.png", k); 
        temp_grid->Threshold(0, 0, 1);
        temp_grid->WriteFile(output_filename);
    }
    fprintf(stderr, "Number of Walls: %d\n", walls.size());

    for (int w = 0; w < walls.size(); w++) {
        Wall* wall = walls[w];
        R2Grid* my_grid = new R2Grid(resolution, resolution);
        my_grid->SetWorldToGridTransformation(world_to_grid_transformation);

        for (int t = 0; t < wall->triangles.size(); t++) {
            R3Triangle* triangle= wall->triangles[t];

                // Create new points
                R2Point v0 = R2Point(triangle->V0()->Position().X(), triangle->V0()->Position().Y());
                R2Point v1 = R2Point(triangle->V1()->Position().X(), triangle->V1()->Position().Y());
                R2Point v2 = R2Point(triangle->V2()->Position().X(), triangle->V2()->Position().Y());

                if (AllVerticesOutOfGrid(temp_grid, v0, v1, v2)) continue;
                std::vector<R2Point> v = SanitizePoints(temp_grid, v0, v1, v2);

                if (VertexOutOfGrid(temp_grid, v[0]) || VertexOutOfGrid(temp_grid, v[1]) || VertexOutOfGrid(temp_grid, v[2]))
                    continue;
               
                my_grid->RasterizeWorldTriangle(v[0], v[1], v[2], 1);

        }    
        char output_filename[1024];
        sprintf(output_filename, "Wall_%d.png", w);
        fprintf(stderr, "Num Triangles in wall %d: %d", w, wall->triangles.size());
        my_grid->Threshold(0, 0, 1);
        my_grid->WriteFile(output_filename);
    }

    temp_grid->Threshold(0, 0, 1);
    grid->Add(*temp_grid);
    return grid;
}

static R2Grid*
DrawObject(R3SceneNode* obj, R2Grid *grid, R2Vector translation, RNAngle theta, R2Vector *dist_ptr = NULL)
{
    R2Grid* temp_grid = new R2Grid(resolution, resolution);

    P5DObject *p5d_obj = (P5DObject *) obj->Data();

    fprintf(stderr, "Drawing  %s (%s)...\n", obj->Name(), p5d_obj->className);

    R2Vector centroid = R2Vector(obj->Centroid().X(), obj->Centroid().Y());

    // Hold
    R2Affine world_to_grid_transformation = R2identity_affine;
    if (dist_ptr != NULL) {
        world_to_grid_transformation.Translate(*dist_ptr * pixels_to_meters);
        world_to_grid_transformation.Translate(-1.0 * *dist_ptr);
    }
    world_to_grid_transformation.Translate(translation);
    world_to_grid_transformation.Translate(centroid);
    world_to_grid_transformation.Scale(pixels_to_meters);
    world_to_grid_transformation.Rotate(-1.0 * theta);
    world_to_grid_transformation.Translate(-1.0 * centroid);

    temp_grid->SetWorldToGridTransformation(world_to_grid_transformation);
    
    // For all R3SceneElements in the R3SceneNode
    for (int k = 0; k < obj->NElements(); k++) {
        R3SceneElement* el = obj->Element(k);
        
        // For all R3Shapes in the R3SceneElements    
        for (int l = 0; l < el->NShapes(); l++) {

            R3Shape* shape = el->Shape(l);
            R3TriangleArray* arr = (R3TriangleArray*) shape;
            
            // For all R3Triangles in the R3TriangleArray
            for (int t = 0; t < arr->NTriangles(); t++) {
                R3Triangle *triangle = arr->Triangle(t);

                // Create new points
                R2Point v0 = R2Point(triangle->V0()->Position().X(), triangle->V0()->Position().Y());
                R2Point v1 = R2Point(triangle->V1()->Position().X(), triangle->V1()->Position().Y());
                R2Point v2 = R2Point(triangle->V2()->Position().X(), triangle->V2()->Position().Y());

                if (AllVerticesOutOfGrid(temp_grid, v0, v1, v2)) continue;
                std::vector<R2Point> v = SanitizePoints(temp_grid, v0, v1, v2);
                
                v0 = temp_grid->GridPosition(v[0]);
                v1 = temp_grid->GridPosition(v[1]);
                v2 = temp_grid->GridPosition(v[2]);

                if (VertexOutOfGrid(temp_grid, v[0]) || VertexOutOfGrid(temp_grid, v[1]) || VertexOutOfGrid(temp_grid, v[2]))
                    continue;

                temp_grid->RasterizeWorldTriangle(v[0], v[1], v[2], 1);
            }
        }
    }

    
    temp_grid->Threshold(0, 0, 1);
    grid->Add(*temp_grid);
    return grid;
}



static int
WriteGrids(R3Scene *scene)
{
  // Create the output directory
  char cmd[1024];
  sprintf(cmd, "mkdir -p %s", output_grid_directory);
  system(cmd);

  // Create mapping of ids to object categories
  LoadIdToCategoryMap();

  // Create mapping from category to images of that category
  std::map<std::string, std::map<std::string, int>> cat_counts;
  std::map<std::string, std::map<std::string, R2Grid*>> grids;

  std::vector<R3SceneNode*> object_nodes;
  std::vector<R3SceneNode*> room_nodes;
  std::vector<R3SceneNode*> wall_nodes;
  std::vector<R3SceneNode*> floor_nodes;

  int obj_num = 0;
  for (int i = 0; i < scene->NNodes(); i++) {
    R3SceneNode* node = scene->Node(i);

    std::string name (node->Name());
    if (std::string::npos != name.find("Object")) // probably a better way with casting
    {
        fprintf(stdout, "%d : %s\n", obj_num++, name.c_str());
        object_nodes.push_back(node);
    }
    if (std::string::npos != name.find("Room"))
        room_nodes.push_back(node);
    if (std::string::npos != name.find("Floor"))
        floor_nodes.push_back(node);
    if (std::string::npos != name.find("Wall"))
        wall_nodes.push_back(node);    
  }

  // Populate Map
  for (int i = 0; i < object_nodes.size(); i++) {
    R3SceneNode* src_obj = object_nodes[i];
    std::string src_cat = GetObjectCategory(src_obj);
    
    /*for (int j = 0; j < object_nodes.size(); j++) {
        if (i == j) continue;
        
        R3SceneNode* dst_obj = object_nodes[j];
        std::string dst_cat = GetObjectCategory(dst_obj);

        grids[src_cat][dst_cat] = new R2Grid(resolution, resolution); 
    }*/
    
    
    for (int w = 0; w < wall_nodes.size(); w++) {
        R3SceneNode* wall_node = wall_nodes[w];
        grids[src_cat]["wall"] = new R2Grid(resolution, resolution);
    }
    
  }

  for (int i = 0; i < object_nodes.size(); i++) {
    R3SceneNode* src_obj = object_nodes[i];
    std::string src_cat = GetObjectCategory(src_obj);
    
    // Translation constants
    float a = 0.5 * (resolution - 1) - src_obj->Centroid().X();
    float b = 0.5 * (resolution - 1) - src_obj->Centroid().Y();
    R2Vector translation(a, b);
    
    // Rotation constants
    P5DObject *p5d_obj = (P5DObject *) src_obj->Data();
    RNAngle theta = p5d_obj->a;
    
    if (!strcmp(p5d_obj->className, "Door")) theta += RN_PI_OVER_TWO;
    else if (!strcmp(p5d_obj->className, "Window")) theta += RN_PI_OVER_TWO;
    
    if (p5d_obj->fX) theta += RN_PI;
    if (p5d_obj->fY) theta += RN_PI;

    /*for (int j = 0; j < object_nodes.size(); j++) {
        if (i == j) continue;
        
        R3SceneNode* dst_obj = object_nodes[j];
        std::string dst_cat = GetObjectCategory(dst_obj);
        
        R2Grid *grid = grids[src_cat][dst_cat];
        
        DrawObject(src_obj, grid, translation, theta);

        R3Vector dist3d = (dst_obj->Centroid() - src_obj->Centroid());
        R2Vector dist = R2Vector(dist3d.X(), dist3d.Y());
        DrawObject(dst_obj, grid, translation, theta, &dist);
    }*/

    for (int w = 0; w < wall_nodes.size(); w++) {
        R3SceneNode* wall_node = wall_nodes[w];

        R2Grid *grid = grids[src_cat]["wall"];

        DrawObject(src_obj, grid, translation, theta);

        R3Vector dist3d = (wall_node->Centroid() - src_obj->Centroid());
        R2Vector dist = R2Vector(dist3d.X(), dist3d.Y());

        DrawWall(wall_node, grid, translation, theta, &dist);
    }
  }

  fprintf(stderr, "Number of Walls: %d\n", wall_nodes.size());

  for (auto it : grids) {
    std::string src_cat = it.first;
    std::map<std::string, R2Grid*> map = it.second;

    for (auto it2 : map) {
        std::string dst_cat = it2.first;
        R2Grid* grid    = it2.second;

        char output_filename[1024];
        sprintf(output_filename, "%s/%s___%s.grd", output_grid_directory, 
                src_cat.c_str(), dst_cat.c_str());
        if (!WriteGrid(grid, output_filename)) return 0;
        
        //fprintf(stdout, "\nStarting %s...\n", output_filename); 
        //grid->Print();
    }
  }

  // Return success
  return 1;
}




static int 
ParseArgs(int argc, char **argv)
{
  // Parse arguments
  argc--; argv++;
  while (argc > 0) {
    if ((*argv)[0] == '-') {
      if (!strcmp(*argv, "-v")) print_verbose = 1; 
      else if (!strcmp(*argv, "-debug")) print_debug = 1; 
      else if (!strcmp(*argv, "-data_directory")) { argc--; argv++; input_data_directory = *argv; }
      else { 
        fprintf(stderr, "Invalid program argument: %s", *argv); 
        exit(1); 
      }
      argv++; argc--;
    }
    else {
      if (!input_project_name) input_project_name = *argv;
      else if (!output_grid_directory) output_grid_directory = *argv;
      else { fprintf(stderr, "Invalid program argument: %s", *argv); exit(1); }
      argv++; argc--;
    }
  }

  // Check filenames
  if (!input_project_name || !output_grid_directory) {
    fprintf(stderr, "Usage: p5dview inputprojectfile outputgriddirectory\n");
    return 0;
  }

  // Return OK status 
  return 1;
}



int main(int argc, char **argv)
{
  // Parse program arguments
  if (!ParseArgs(argc, argv)) exit(-1);

  // Read project
  P5DProject *project = ReadProject(input_project_name);
  if (!project) exit(-1);

  // Create scene
  R3Scene *scene = CreateScene(project);
  if (!scene) exit(-1);

  // Write grids
  if (!WriteGrids(scene)) exit(-1);
  
  // Return success 
  return 0;
}



