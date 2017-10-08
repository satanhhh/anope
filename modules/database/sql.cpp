/*
 * Anope IRC Services
 *
 * Copyright (C) 2011-2017 Anope Team <team@anope.org>
 *
 * This file is part of Anope. Anope is free software; you can
 * redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see see <http://www.gnu.org/licenses/>.
 */

#include "module.h"
#include "modules/sql.h"

using namespace SQL;

class DBSQL : public Module, public Pipe
	, public EventHook<Event::SerializeEvents>
{
 private:
	bool transaction = false;
	bool inited = false;
	Anope::string prefix;
	ServiceReference<Provider> SQL;

	Result Run(const Query &query)
	{
		if (!SQL)
			return Result();

		if (!inited)
		{
			inited = true;
			for (const Query &q : SQL->InitSchema(prefix))
				SQL->RunQuery(q);
		}

		return SQL->RunQuery(query);
	}

	void StartTransaction()
	{
		if (!SQL || transaction)
			return;

		Run(SQL->BeginTransaction());

		transaction = true;
		Notify();
	}

	void Commit()
	{
		if (!SQL || !transaction)
			return;

		Run(SQL->Commit());

		transaction = false;
	}

 public:
	DBSQL(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, DATABASE | VENDOR)
		, EventHook<Event::SerializeEvents>(this)
	{
	}

	void OnNotify() override
	{
		Commit();
		Serialize::GC();
	}

	void OnReload(Configuration::Conf *conf) override
	{
		Configuration::Block *block = conf->GetModule(this);
		this->SQL = ServiceReference<Provider>(block->Get<Anope::string>("engine"));
		this->prefix = block->Get<Anope::string>("prefix", "anope_");
		inited = false;
	}

	EventReturn OnSerializeList(Serialize::TypeBase *type, std::vector<Serialize::ID> &ids) override
	{
		StartTransaction();

		ids.clear();

		Query query = "SELECT `id` FROM `" + prefix + type->GetName() + "`";
		Result res = Run(query);
		for (int i = 0; i < res.Rows(); ++i)
		{
			Serialize::ID id = convertTo<Serialize::ID>(res.Get(i, "id"));
			ids.push_back(id);
		}

		return EVENT_ALLOW;
	}

	EventReturn OnSerializeFind(Serialize::TypeBase *type, Serialize::FieldBase *field, const Anope::string &value, Serialize::ID &id) override
	{
		if (!SQL)
			return EVENT_CONTINUE;

		StartTransaction();

		for (Query &q : SQL->CreateTable(prefix, type))
			Run(q);

		for (Query &q : SQL->AlterTable(prefix, type, field))
			Run(q);

		for (const Query &q : SQL->CreateIndex(prefix + type->GetName(), field->serialize_name))
			Run(q);

		Query query = SQL->SelectFind(prefix + type->GetName(), field->serialize_name);

		query.SetValue("value", value);
		Result res = Run(query);
		if (res.Rows())
			try
			{
				id = convertTo<Serialize::ID>(res.Get(0, "id"));
				return EVENT_ALLOW;
			}
			catch (const ConvertException &)
			{
			}

		return EVENT_CONTINUE;
	}

 private:
	bool GetValue(Serialize::Object *object, Serialize::FieldBase *field, SQL::Result::Value &v)
	{
		StartTransaction();

		Query query = "SELECT `" + field->serialize_name + "` FROM `" + prefix + object->GetSerializableType()->GetName() + "` WHERE `id` = @id@";
		query.SetValue("id", object->id, false);
		Result res = Run(query);

		if (res.Rows() == 0)
			return false;

		v = res.GetValue(0, field->serialize_name);
		return true;
	}

	void GetRefs(Serialize::Object *object, Serialize::TypeBase *type, std::vector<Serialize::Edge> &edges)
	{
		for (Serialize::FieldBase *field : type->GetFields())
		{
			if (field->is_object && object->GetSerializableType()->GetName() == field->GetTypeName())
			{
				Anope::string table = prefix + type->GetName();

				Query query = "SELECT id FROM " + table +
					" WHERE " + field->serialize_name + " = @id@";

				query.SetValue("id", object->id, false);

				Result res = Run(query);
				for (int i = 0; i < res.Rows(); ++i)
				{
					Serialize::ID id = convertTo<Serialize::ID>(res.Get(i, "id"));

					Serialize::Object *other = type->Require(id);
					if (other == nullptr)
					{
						Anope::Logger.Debug("Unable to require id {0} type {1}", id, type->GetName());
						continue;
					}

					// other type, other field, direction
					edges.emplace_back(other, field, false);
				}
			}
		}
	}

 public:
	EventReturn OnSerializeGet(Serialize::Object *object, Serialize::FieldBase *field, Anope::string &value) override
	{
		SQL::Result::Value v;

		if (!GetValue(object, field, v))
			return EVENT_CONTINUE;

		value = v.value;
		return EVENT_ALLOW;
	}

	EventReturn OnSerializeGetRefs(Serialize::Object *object, Serialize::TypeBase *type, std::vector<Serialize::Edge> &edges) override
	{
		StartTransaction();

		edges.clear();

		if (type == nullptr)
		{
			for (Serialize::TypeBase *t : Serialize::TypeBase::GetTypes())
				GetRefs(object, t, edges);
		}
		else
		{
			GetRefs(object, type, edges);
		}

		return EVENT_ALLOW;
	}

	EventReturn OnSerializeDeref(Serialize::ID id, Serialize::TypeBase *type) override
	{
		StartTransaction();

		Query query = "SELECT `id` FROM `" + prefix + type->GetName() + "` WHERE `id` = @id@";
		query.SetValue("id", id, false);
		Result res = Run(query);
		if (res.Rows() == 0)
			return EVENT_CONTINUE;
		return EVENT_ALLOW;
	}

	EventReturn OnSerializeGetSerializable(Serialize::Object *object, Serialize::FieldBase *field, Anope::string &type, Serialize::ID &value) override
	{
		StartTransaction();

		Query query = "SELECT `" + field->serialize_name + "` FROM `" + prefix + object->GetSerializableType()->GetName() + "` "
			"WHERE id = @id@";
		query.SetValue("id", object->id, false);
		Result res = Run(query);

		if (res.Rows() == 0)
			return EVENT_CONTINUE;

		type = field->GetTypeName();
		try
		{
			value = convertTo<Serialize::ID>(res.Get(0, field->serialize_name));
		}
		catch (const ConvertException &ex)
		{
			return EVENT_STOP;
		}

		return EVENT_ALLOW;
	}

 private:
	void DoSet(Serialize::Object *object, Serialize::FieldBase *field, bool is_object, const Anope::string *value)
	{
		if (!SQL)
			return;

		StartTransaction();

		for (Query &q : SQL->CreateTable(prefix, object->GetSerializableType()))
			Run(q);

		for (Query &q : SQL->AlterTable(prefix, object->GetSerializableType(), field))
			Run(q);

		Query q;
		q.SetValue("id", object->id, false);
		if (value)
			q.SetValue(field->serialize_name, *value, !is_object);
		else
			q.SetNull(field->serialize_name);

		for (Query &q2 : SQL->Replace(prefix + object->GetSerializableType()->GetName(), q, { "id" }))
			Run(q2);
	}

 public:
	EventReturn OnSerializeSet(Serialize::Object *object, Serialize::FieldBase *field, const Anope::string &value) override
	{
		DoSet(object, field, false, &value);
		return EVENT_STOP;
	}

	EventReturn OnSerializeSetSerializable(Serialize::Object *object, Serialize::FieldBase *field, Serialize::Object *value) override
	{
		if (!SQL)
			return EVENT_CONTINUE;

		StartTransaction();

		if (value)
		{
			Anope::string v = stringify(value->id);
			DoSet(object, field, true, &v);
		}
		else
		{
			DoSet(object, field, true, nullptr);
		}

		return EVENT_STOP;
	}

	EventReturn OnSerializeUnset(Serialize::Object *object, Serialize::FieldBase *field) override
	{
		DoSet(object, field, false, nullptr);
		return EVENT_STOP;
	}

	EventReturn OnSerializeUnsetSerializable(Serialize::Object *object, Serialize::FieldBase *field) override
	{
		DoSet(object, field, true, nullptr);
		return EVENT_STOP;
	}

	EventReturn OnSerializeHasField(Serialize::Object *object, Serialize::FieldBase *field) override
	{
		if (field->is_object)
		{
			Anope::string type;
			Serialize::ID id;

			EventReturn er = OnSerializeGetSerializable(object, field, type, id);

			if (er != EVENT_ALLOW)
				return EVENT_CONTINUE;

			field->UnserializeFromString(object, type + ":" + stringify(id));
			return EVENT_STOP;
		}
		else
		{
			SQL::Result::Value v;

			if (!GetValue(object, field, v))
				return EVENT_CONTINUE;

			if (v.null)
				return EVENT_CONTINUE;

			field->UnserializeFromString(object, v.value);
			return EVENT_STOP;
		}
	}

	EventReturn OnSerializableGetId(Serialize::TypeBase *type, Serialize::ID &id) override
	{
		if (!SQL)
			return EVENT_CONTINUE;

		StartTransaction();

		for (Query &q : SQL->CreateTable(prefix, type))
			Run(q);

		id = SQL->GetID(prefix, type->GetName());
		return EVENT_ALLOW;
	}

	void OnSerializableCreate(Serialize::Object *object) override
	{
	}

	void OnSerializableDelete(Serialize::Object *object) override
	{
		StartTransaction();

		Serialize::TypeBase *type = object->GetSerializableType();

		Query query("DELETE FROM `" + prefix + type->GetName() + "` WHERE `id` = " + stringify(object->id));
		Run(query);
	}
};

MODULE_INIT(DBSQL)

