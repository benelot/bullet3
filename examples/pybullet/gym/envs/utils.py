import pybullet as p
from robot_bases import BodyPart


def get_cube(x, y, z):
	body = p.loadURDF("cube_small.urdf", x, y, z)
	part_name, _ = p.getBodyInfo(body, 0)
	part_name = part_name.decode("utf8")
	bodies = [body]
	return BodyPart(part_name, bodies, 0, -1)

def get_sphere(x, y, z):
	body = p.loadURDF("sphere_small.urdf", x, y, z)
	part_name, _ = p.getBodyInfo(body, 0)
	part_name = part_name.decode("utf8")
	bodies = [body]
	return BodyPart(part_name, bodies, 0, -1)