# Optimizing Voxel rendering

## Chunk Rendering

First we should chunk stuff and render stuff in chunks

take for example this

``` c

void GenerateChunkMesh()
{
    for (int x = 0; x < 32; x++)
        for (int y = 0; y < 32; y++)
            for (int z = 0; z < 32; z++)
            {
                var voxel = GetVoxel(x, y, z);

                // Skip empty voxels
                if (voxel.Empty)
                    continue;
                
                //Ensure there is no voxel above us
                if (GetVoxel(x, y + 1, z).Empty)
                    AddTopFace();
                
                // Ensure no voxel below us
                if (GetVoxel(x, y - 1, z).Empty)
                    AddBottomFace();

                //... And so on, for all right, left, front, back faces
            }
}
```

## Combining Voxels With Same textures

Literally just greedy meshing

So far this is enough, but there is still more optimizations like how triangls store vec3 position, vec3 normal, and int textureID being compressed to save memory but ehhh. Guy compresses stuff with byte values but do we really need it?

## Voxel Normal Compression

We can enum the normal directions using an int instead of storing the actual normals since voxels are cubes, but we dont really need this yet especially since we wont use pure voxels just yet