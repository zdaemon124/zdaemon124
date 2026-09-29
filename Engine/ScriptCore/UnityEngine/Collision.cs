using System;
using IndeetsEngine.Runtime;

namespace UnityEngine
{
    public struct ContactPoint
    {
        internal Vector3 m_Point;
        internal Vector3 m_Normal;
        internal Collider m_This;
        internal Collider m_Other;

        public Vector3 point => m_Point;
        /// <summary>Points away from the other collider, towards this one.</summary>
        public Vector3 normal => m_Normal;
        public Collider thisCollider => m_This;
        public Collider otherCollider => m_Other;
        public float separation => 0f;
    }

    /// <summary>Information passed to OnCollisionEnter / Stay / Exit.</summary>
    public class Collision
    {
        internal GameObject m_Other;
        internal Collider m_Collider;
        internal ContactPoint[] m_Contacts;
        internal Vector3 m_RelativeVelocity;

        /// <summary>The object hit (for a compound body, the object owning the rigidbody).</summary>
        public GameObject gameObject => m_Other;
        public Transform transform => m_Other != null ? m_Other.transform : null;
        public Collider collider => m_Collider;
        public Rigidbody rigidbody => m_Collider != null ? m_Collider.attachedRigidbody : null;
        public Rigidbody body => rigidbody;
        public Vector3 relativeVelocity => m_RelativeVelocity;
        public Vector3 impulse => Vector3.zero;
        public int contactCount => m_Contacts.Length;
        public ContactPoint[] contacts => m_Contacts;

        public ContactPoint GetContact(int index) => m_Contacts[index];

        public int GetContacts(ContactPoint[] contacts)
        {
            int n = Math.Min(contacts.Length, m_Contacts.Length);
            Array.Copy(m_Contacts, contacts, n);
            return n;
        }

        public int GetContacts(System.Collections.Generic.List<ContactPoint> contacts)
        {
            contacts.Clear();
            contacts.AddRange(m_Contacts);
            return m_Contacts.Length;
        }
    }
}
